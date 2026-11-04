#include <gtest/gtest.h>
#include "proxy/reverse_proxy.h"
#include "balancer/round_robin.h"
#include "network/socket.h"
#include "http/http_parser.h"
#include <thread>
#include <atomic>
#include <chrono>

TEST(ReverseProxyTest, InterceptsInternalEndpoints) {
    auto b1 = std::make_shared<balancer::BackendServer>();
    b1->host = "127.0.0.1"; b1->port = 9999;
    auto lb = std::make_shared<balancer::RoundRobinBalancer>(std::vector<std::shared_ptr<balancer::BackendServer>>{b1});

    config::ServerConfig cfg;
    proxy::ReverseProxy proxy(lb, cfg);

    http::HttpRequest req_health;
    req_health.path = "/health";
    auto resp_health = proxy.handle_request(req_health, "127.0.0.1");
    EXPECT_EQ(resp_health.status_code, http::StatusCode::OK);
    EXPECT_NE(resp_health.body.find("\"status\": \"UP\""), std::string::npos);

    http::HttpRequest req_metrics;
    req_metrics.path = "/metrics";
    auto resp_metrics = proxy.handle_request(req_metrics, "127.0.0.1");
    EXPECT_EQ(resp_metrics.status_code, http::StatusCode::OK);
    EXPECT_NE(resp_metrics.body.find("loadbalancer_requests_total"), std::string::npos);

    http::HttpRequest req_backends;
    req_backends.path = "/backends";
    auto resp_backends = proxy.handle_request(req_backends, "127.0.0.1");
    EXPECT_EQ(resp_backends.status_code, http::StatusCode::OK);
    EXPECT_NE(resp_backends.body.find("\"address\": \"127.0.0.1:9999\""), std::string::npos);
}

TEST(ReverseProxyTest, ForwardsRequestToMockBackend) {
    // 1. Setup mock backend TCP server
    net::ServerSocket mock_backend;
    ASSERT_TRUE(mock_backend.bind_and_listen("127.0.0.1", 0));
    uint16_t backend_port = mock_backend.get_bound_port();

    std::atomic<bool> backend_received{false};
    std::string received_x_forwarded_for;

    std::thread backend_thread([&]() {
        net::Socket client = mock_backend.accept();
        ASSERT_TRUE(client.is_valid());

        char buf[2048];
        ssize_t bytes = client.receive(buf, sizeof(buf) - 1);
        if (bytes > 0) {
            buf[bytes] = '\0';
            std::string raw(buf, bytes);
            if (raw.find("X-Forwarded-For: 192.168.1.50") != std::string::npos) {
                received_x_forwarded_for = "192.168.1.50";
            }
            backend_received.store(true);
        }

        std::string reply = "HTTP/1.1 200 OK\r\nContent-Length: 13\r\n\r\nHello Backend";
        client.send_string(reply);
    });

    // 2. Setup ReverseProxy pointing to mock backend
    auto node = std::make_shared<balancer::BackendServer>();
    node->host = "127.0.0.1";
    node->port = backend_port;

    auto lb = std::make_shared<balancer::RoundRobinBalancer>(std::vector<std::shared_ptr<balancer::BackendServer>>{node});
    config::ServerConfig cfg;
    cfg.backend_timeout = std::chrono::milliseconds(2000);
    proxy::ReverseProxy proxy(lb, cfg);

    http::HttpRequest client_req;
    client_req.method = http::Method::GET;
    client_req.uri = "/api/data";
    client_req.path = "/api/data";

    auto resp = proxy.handle_request(client_req, "192.168.1.50");

    backend_thread.join();

    EXPECT_TRUE(backend_received.load());
    EXPECT_EQ(received_x_forwarded_for, "192.168.1.50");
    EXPECT_EQ(resp.status_code, http::StatusCode::OK);
    EXPECT_EQ(resp.body, "Hello Backend");
}

TEST(ReverseProxyTest, StripsHopByHopAndConnectionNominatedHeaders) {
    net::ServerSocket mock_backend;
    ASSERT_TRUE(mock_backend.bind_and_listen("127.0.0.1", 0));
    uint16_t backend_port = mock_backend.get_bound_port();

    std::string received_raw_request;

    std::thread backend_thread([&]() {
        net::Socket client = mock_backend.accept();
        ASSERT_TRUE(client.is_valid());

        char buf[4096];
        ssize_t bytes = client.receive(buf, sizeof(buf) - 1);
        if (bytes > 0) {
            buf[bytes] = '\0';
            received_raw_request = std::string(buf, bytes);
        }

        std::string reply = "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nOK";
        client.send_string(reply);
    });

    auto node = std::make_shared<balancer::BackendServer>();
    node->host = "127.0.0.1";
    node->port = backend_port;

    auto lb = std::make_shared<balancer::RoundRobinBalancer>(std::vector<std::shared_ptr<balancer::BackendServer>>{node});
    config::ServerConfig cfg;
    cfg.backend_timeout = std::chrono::milliseconds(2000);
    proxy::ReverseProxy proxy(lb, cfg);

    http::HttpRequest client_req;
    client_req.method = http::Method::GET;
    client_req.uri = "/hopbyhop";
    client_req.path = "/hopbyhop";
    client_req.set_header("Connection", "Keep-Alive, X-Nominated-Custom");
    client_req.set_header("Keep-Alive", "timeout=5, max=1000");
    client_req.set_header("Proxy-Authorization", "Basic dXNlcjpwYXNz");
    client_req.set_header("Upgrade", "websocket");
    client_req.set_header("X-Nominated-Custom", "strip-me");
    client_req.set_header("X-Regular-Header", "preserve-me");

    auto resp = proxy.handle_request(client_req, "10.0.0.1");
    backend_thread.join();

    EXPECT_EQ(resp.status_code, http::StatusCode::OK);
    // Hop-by-hop headers must NOT reach the backend
    EXPECT_EQ(received_raw_request.find("Keep-Alive:"), std::string::npos);
    EXPECT_EQ(received_raw_request.find("Proxy-Authorization:"), std::string::npos);
    EXPECT_EQ(received_raw_request.find("Upgrade:"), std::string::npos);
    EXPECT_EQ(received_raw_request.find("X-Nominated-Custom:"), std::string::npos);
    // Regular headers must be preserved
    EXPECT_NE(received_raw_request.find("X-Regular-Header: preserve-me"), std::string::npos);
}

TEST(ReverseProxyTest, ReplacesClientSuppliedXForwardedForToPreventSpoofing) {
    net::ServerSocket mock_backend;
    ASSERT_TRUE(mock_backend.bind_and_listen("127.0.0.1", 0));
    uint16_t backend_port = mock_backend.get_bound_port();

    std::string received_raw_request;

    std::thread backend_thread([&]() {
        net::Socket client = mock_backend.accept();
        ASSERT_TRUE(client.is_valid());

        char buf[4096];
        ssize_t bytes = client.receive(buf, sizeof(buf) - 1);
        if (bytes > 0) {
            buf[bytes] = '\0';
            received_raw_request = std::string(buf, bytes);
        }

        std::string reply = "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nOK";
        client.send_string(reply);
    });

    auto node = std::make_shared<balancer::BackendServer>();
    node->host = "127.0.0.1";
    node->port = backend_port;

    auto lb = std::make_shared<balancer::RoundRobinBalancer>(std::vector<std::shared_ptr<balancer::BackendServer>>{node});
    config::ServerConfig cfg;
    cfg.backend_timeout = std::chrono::milliseconds(2000);
    proxy::ReverseProxy proxy(lb, cfg);

    http::HttpRequest client_req;
    client_req.method = http::Method::GET;
    client_req.uri = "/spoof";
    client_req.path = "/spoof";
    // Client tries to spoof its IP as 8.8.8.8
    client_req.set_header("X-Forwarded-For", "8.8.8.8");

    // Real client IP is 172.16.0.4
    auto resp = proxy.handle_request(client_req, "172.16.0.4");
    backend_thread.join();

    EXPECT_EQ(resp.status_code, http::StatusCode::OK);
    // The forwarded request must have 172.16.0.4, NOT 8.8.8.8
    EXPECT_EQ(received_raw_request.find("8.8.8.8"), std::string::npos);
    EXPECT_NE(received_raw_request.find("X-Forwarded-For: 172.16.0.4"), std::string::npos);
}

TEST(ReverseProxyTest, HandlesBackendDownReturnsBadGateway) {
    auto node = std::make_shared<balancer::BackendServer>();
    node->host = "127.0.0.1";
    node->port = 65432; // Unused port where nothing is listening

    auto lb = std::make_shared<balancer::RoundRobinBalancer>(std::vector<std::shared_ptr<balancer::BackendServer>>{node});
    config::ServerConfig cfg;
    cfg.backend_timeout = std::chrono::milliseconds(200);
    proxy::ReverseProxy proxy(lb, cfg);

    http::HttpRequest client_req;
    client_req.method = http::Method::GET;
    client_req.uri = "/test";
    client_req.path = "/test";

    auto resp = proxy.handle_request(client_req, "127.0.0.1");
    EXPECT_EQ(resp.status_code, http::StatusCode::BadGateway);
}

TEST(ReverseProxyTest, HandlesInterim100ContinueResponse) {
    net::ServerSocket mock_backend;
    ASSERT_TRUE(mock_backend.bind_and_listen("127.0.0.1", 0));
    uint16_t backend_port = mock_backend.get_bound_port();

    std::thread backend_thread([&]() {
        net::Socket client = mock_backend.accept();
        ASSERT_TRUE(client.is_valid());

        char buf[1024];
        ssize_t bytes = client.receive(buf, sizeof(buf) - 1);
        (void)bytes;

        // First send 100 Continue, then 200 OK
        std::string interim = "HTTP/1.1 100 Continue\r\n\r\n";
        client.send_string(interim);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        std::string final_resp = "HTTP/1.1 200 OK\r\nContent-Length: 12\r\n\r\nFinal Result";
        client.send_string(final_resp);
    });

    auto node = std::make_shared<balancer::BackendServer>();
    node->host = "127.0.0.1";
    node->port = backend_port;

    auto lb = std::make_shared<balancer::RoundRobinBalancer>(std::vector<std::shared_ptr<balancer::BackendServer>>{node});
    config::ServerConfig cfg;
    cfg.backend_timeout = std::chrono::milliseconds(2000);
    proxy::ReverseProxy proxy(lb, cfg);

    http::HttpRequest client_req;
    client_req.method = http::Method::POST;
    client_req.uri = "/upload";
    client_req.path = "/upload";
    client_req.body = "data";

    auto resp = proxy.handle_request(client_req, "127.0.0.1");
    backend_thread.join();

    EXPECT_EQ(resp.status_code, http::StatusCode::OK);
    EXPECT_EQ(resp.body, "Final Result");
}

TEST(ReverseProxyTest, Rejects101SwitchingProtocolsWithBadGateway) {
    net::ServerSocket mock_backend;
    ASSERT_TRUE(mock_backend.bind_and_listen("127.0.0.1", 0));
    uint16_t backend_port = mock_backend.get_bound_port();

    std::thread backend_thread([&]() {
        net::Socket client = mock_backend.accept();
        ASSERT_TRUE(client.is_valid());

        char buf[1024];
        ssize_t bytes = client.receive(buf, sizeof(buf) - 1);
        (void)bytes;

        std::string ws_resp = "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n\r\n";
        client.send_string(ws_resp);
    });

    auto node = std::make_shared<balancer::BackendServer>();
    node->host = "127.0.0.1";
    node->port = backend_port;

    auto lb = std::make_shared<balancer::RoundRobinBalancer>(std::vector<std::shared_ptr<balancer::BackendServer>>{node});
    config::ServerConfig cfg;
    cfg.backend_timeout = std::chrono::milliseconds(2000);
    proxy::ReverseProxy proxy(lb, cfg);

    http::HttpRequest client_req;
    client_req.method = http::Method::GET;
    client_req.uri = "/ws";
    client_req.path = "/ws";

    auto resp = proxy.handle_request(client_req, "127.0.0.1");
    backend_thread.join();

    EXPECT_EQ(resp.status_code, http::StatusCode::BadGateway);
}

TEST(ReverseProxyTest, Expect100ContinueEndToEndExchange) {
    // 1. Setup mock backend TCP server
    net::ServerSocket mock_backend;
    ASSERT_TRUE(mock_backend.bind_and_listen("127.0.0.1", 0));
    uint16_t backend_port = mock_backend.get_bound_port();

    std::atomic<bool> backend_saw_100_continue_header{false};
    std::atomic<bool> backend_received_full_body{false};

    std::thread backend_thread([&]() {
        net::Socket proxy_conn = mock_backend.accept();
        ASSERT_TRUE(proxy_conn.is_valid());

        // Read request headers from proxy
        char buf[2048];
        ssize_t n = proxy_conn.receive(buf, sizeof(buf) - 1);
        ASSERT_GT(n, 0);
        buf[n] = '\0';
        std::string req_hdr(buf, static_cast<size_t>(n));

        if (req_hdr.find("Expect: 100-continue") != std::string::npos ||
            req_hdr.find("expect: 100-continue") != std::string::npos) {
            backend_saw_100_continue_header.store(true);
        }

        // Send 100 Continue to proxy
        std::string interim = "HTTP/1.1 100 Continue\r\n\r\n";
        proxy_conn.send_all(interim.data(), interim.size());

        // Now read request body transmitted by client through proxy
        std::string expected_body = "PayloadDataTransmittedAfterContinue";
        std::string body_received;
        char body_buf[1024];
        while (body_received.size() < expected_body.size()) {
            ssize_t b = proxy_conn.receive(body_buf, sizeof(body_buf));
            if (b <= 0) break;
            body_received.append(body_buf, static_cast<size_t>(b));
        }

        if (body_received == expected_body) {
            backend_received_full_body.store(true);
        }

        // Send final 200 OK response
        std::string final_resp = "HTTP/1.1 200 OK\r\nContent-Length: 7\r\nConnection: close\r\n\r\nSuccess";
        proxy_conn.send_all(final_resp.data(), final_resp.size());
    });

    // 2. Setup mock client TCP connection pair
    net::ServerSocket client_listener;
    ASSERT_TRUE(client_listener.bind_and_listen("127.0.0.1", 0));
    uint16_t client_port = client_listener.get_bound_port();

    net::ClientSocket client_conn;
    ASSERT_TRUE(client_conn.connect("127.0.0.1", client_port, std::chrono::milliseconds(2000)));
    net::Socket server_side_client_sock = client_listener.accept();
    ASSERT_TRUE(server_side_client_sock.is_valid());

    // 3. Setup reverse proxy
    auto node = std::make_shared<balancer::BackendServer>();
    node->host = "127.0.0.1";
    node->port = backend_port;
    auto lb = std::make_shared<balancer::RoundRobinBalancer>(std::vector<std::shared_ptr<balancer::BackendServer>>{node});
    config::ServerConfig cfg;
    cfg.backend_timeout = std::chrono::milliseconds(3000);
    proxy::ReverseProxy proxy(lb, cfg);

    std::atomic<bool> client_received_100_continue{false};
    std::atomic<bool> client_received_final_200{false};

    std::thread client_thread([&]() {
        // Step A: Client sends headers containing Expect: 100-continue
        std::string payload = "PayloadDataTransmittedAfterContinue";
        std::string headers = "POST /submit HTTP/1.1\r\n"
                              "Host: localhost\r\n"
                              "Expect: 100-continue\r\n"
                              "Content-Length: " + std::to_string(payload.size()) + "\r\n\r\n";
        client_conn.send_all(headers.data(), headers.size());

        // Step B: Client waits for and reads 100 Continue from proxy
        char resp_buf[1024];
        ssize_t bytes = client_conn.receive(resp_buf, sizeof(resp_buf) - 1);
        ASSERT_GT(bytes, 0);
        resp_buf[bytes] = '\0';
        std::string r1(resp_buf, static_cast<size_t>(bytes));
        if (r1.find("100 Continue") != std::string::npos) {
            client_received_100_continue.store(true);
        }

        // Step C: Client transmits request body upon receiving 100 Continue
        client_conn.send_all(payload.data(), payload.size());

        // Step D: Client receives final response
        std::string final_str;
        while (final_str.find("\r\n\r\n") == std::string::npos || final_str.find("Success") == std::string::npos) {
            ssize_t b = client_conn.receive(resp_buf, sizeof(resp_buf) - 1);
            if (b <= 0) break;
            resp_buf[b] = '\0';
            final_str.append(resp_buf, static_cast<size_t>(b));
        }

        if (final_str.find("200 OK") != std::string::npos && final_str.find("Success") != std::string::npos) {
            client_received_final_200.store(true);
        }
    });

    // 4. Server side proxy execution
    std::string read_buf;
    char s_buf[1024];
    http::HttpParser parser;
    http::HttpRequest req;
    size_t consumed = 0;

    while (true) {
        ssize_t n = server_side_client_sock.receive(s_buf, sizeof(s_buf));
        ASSERT_GT(n, 0);
        read_buf.append(s_buf, static_cast<size_t>(n));
        auto parse_res = parser.parse_request(read_buf, req, consumed);
        if (parse_res == http::ParseResult::Complete) {
            read_buf.erase(0, consumed);
            break;
        }
    }

    auto resp = proxy.handle_request(req, "127.0.0.1", &server_side_client_sock, &read_buf);
    std::string resp_str = resp.to_string();
    server_side_client_sock.send_all(resp_str.data(), resp_str.size());

    client_thread.join();
    backend_thread.join();

    EXPECT_TRUE(backend_saw_100_continue_header.load());
    EXPECT_TRUE(client_received_100_continue.load());
    EXPECT_TRUE(backend_received_full_body.load());
    EXPECT_TRUE(client_received_final_200.load());
    EXPECT_EQ(resp.status_code, http::StatusCode::OK);
    EXPECT_EQ(resp.body, "Success");
}

TEST(ReverseProxyTest, Forwards102And103InterimResponses) {
    net::ServerSocket mock_backend;
    ASSERT_TRUE(mock_backend.bind_and_listen("127.0.0.1", 0));
    uint16_t backend_port = mock_backend.get_bound_port();

    std::thread backend_thread([&]() {
        net::Socket proxy_conn = mock_backend.accept();
        ASSERT_TRUE(proxy_conn.is_valid());

        char buf[1024];
        proxy_conn.receive(buf, sizeof(buf));

        // Send 102 Processing
        std::string interim102 = "HTTP/1.1 102 Processing\r\n\r\n";
        proxy_conn.send_all(interim102.data(), interim102.size());
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

        // Send 103 Early Hints
        std::string interim103 = "HTTP/1.1 103 Early Hints\r\nLink: </style.css>; rel=preload\r\n\r\n";
        proxy_conn.send_all(interim103.data(), interim103.size());
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

        // Send final 200 OK
        std::string final_resp = "HTTP/1.1 200 OK\r\nContent-Length: 4\r\n\r\nDone";
        proxy_conn.send_all(final_resp.data(), final_resp.size());
    });

    net::ServerSocket client_listener;
    ASSERT_TRUE(client_listener.bind_and_listen("127.0.0.1", 0));
    uint16_t client_port = client_listener.get_bound_port();

    net::ClientSocket client_conn;
    ASSERT_TRUE(client_conn.connect("127.0.0.1", client_port, std::chrono::milliseconds(2000)));
    net::Socket server_side_client_sock = client_listener.accept();
    ASSERT_TRUE(server_side_client_sock.is_valid());

    auto node = std::make_shared<balancer::BackendServer>();
    node->host = "127.0.0.1";
    node->port = backend_port;
    auto lb = std::make_shared<balancer::RoundRobinBalancer>(std::vector<std::shared_ptr<balancer::BackendServer>>{node});
    config::ServerConfig cfg;
    cfg.backend_timeout = std::chrono::milliseconds(3000);
    proxy::ReverseProxy proxy(lb, cfg);

    std::atomic<bool> saw_102{false};
    std::atomic<bool> saw_103{false};

    std::thread client_thread([&]() {
        char buf[1024];
        std::string accumulated;
        while (accumulated.find("Done") == std::string::npos) {
            ssize_t b = client_conn.receive(buf, sizeof(buf) - 1);
            if (b <= 0) break;
            buf[b] = '\0';
            accumulated.append(buf, static_cast<size_t>(b));
            if (accumulated.find("102 Processing") != std::string::npos) saw_102.store(true);
            if (accumulated.find("103 Early Hints") != std::string::npos) saw_103.store(true);
        }
    });

    http::HttpRequest req;
    req.method = http::Method::GET;
    req.uri = "/async";
    req.path = "/async";
    req.version = "HTTP/1.1";
    req.set_header("Host", "localhost");

    std::string read_buf;
    auto resp = proxy.handle_request(req, "127.0.0.1", &server_side_client_sock, &read_buf);
    std::string resp_str = resp.to_string();
    server_side_client_sock.send_all(resp_str.data(), resp_str.size());

    client_thread.join();
    backend_thread.join();

    EXPECT_TRUE(saw_102.load());
    EXPECT_TRUE(saw_103.load());
    EXPECT_EQ(resp.status_code, http::StatusCode::OK);
    EXPECT_EQ(resp.body, "Done");
}
