#include <gtest/gtest.h>
#include "balancer/round_robin.h"
#include "balancer/least_connections.h"

TEST(LoadBalancerTest, RoundRobinCyclesBackends) {
    auto b1 = std::make_shared<balancer::BackendServer>();
    b1->host = "10.0.0.1"; b1->port = 8081;
    auto b2 = std::make_shared<balancer::BackendServer>();
    b2->host = "10.0.0.2"; b2->port = 8082;
    auto b3 = std::make_shared<balancer::BackendServer>();
    b3->host = "10.0.0.3"; b3->port = 8083;

    balancer::RoundRobinBalancer rr({b1, b2, b3});
    http::HttpRequest dummy_req;

    EXPECT_EQ(rr.select_backend(dummy_req)->port, 8081);
    EXPECT_EQ(rr.select_backend(dummy_req)->port, 8082);
    EXPECT_EQ(rr.select_backend(dummy_req)->port, 8083);
    EXPECT_EQ(rr.select_backend(dummy_req)->port, 8081);
}

TEST(LoadBalancerTest, RoundRobinFiltersUnhealthyBackends) {
    auto b1 = std::make_shared<balancer::BackendServer>();
    b1->host = "10.0.0.1"; b1->port = 8081;
    auto b2 = std::make_shared<balancer::BackendServer>();
    b2->host = "10.0.0.2"; b2->port = 8082;
    b2->is_healthy.store(false); // Unhealthy!

    balancer::RoundRobinBalancer rr({b1, b2});
    http::HttpRequest dummy_req;

    // Both queries should route to b1
    EXPECT_EQ(rr.select_backend(dummy_req)->port, 8081);
    EXPECT_EQ(rr.select_backend(dummy_req)->port, 8081);
}

TEST(LoadBalancerTest, RoundRobinAllUnhealthyReturnsNull) {
    auto b1 = std::make_shared<balancer::BackendServer>();
    b1->host = "10.0.0.1"; b1->port = 8081;
    b1->is_healthy.store(false);

    balancer::RoundRobinBalancer rr({b1});
    http::HttpRequest dummy_req;

    EXPECT_EQ(rr.select_backend(dummy_req), nullptr);
}

TEST(LoadBalancerTest, LeastConnectionsSelectsLeastLoaded) {
    auto b1 = std::make_shared<balancer::BackendServer>();
    b1->host = "10.0.0.1"; b1->port = 8081;
    b1->active_connections.store(10);

    auto b2 = std::make_shared<balancer::BackendServer>();
    b2->host = "10.0.0.2"; b2->port = 8082;
    b2->active_connections.store(2);

    auto b3 = std::make_shared<balancer::BackendServer>();
    b3->host = "10.0.0.3"; b3->port = 8083;
    b3->active_connections.store(5);

    balancer::LeastConnectionsBalancer lc({b1, b2, b3});
    http::HttpRequest dummy_req;

    auto chosen = lc.select_backend(dummy_req);
    ASSERT_NE(chosen, nullptr);
    EXPECT_EQ(chosen->port, 8082); // b2 has min (2)
}

TEST(LoadBalancerTest, ConnectionGuardRAII) {
    auto b = std::make_shared<balancer::BackendServer>();
    b->host = "127.0.0.1"; b->port = 9000;

    EXPECT_EQ(b->active_connections.load(), 0);
    EXPECT_EQ(b->total_requests.load(), 0);

    {
        balancer::BackendConnectionGuard guard(b);
        EXPECT_EQ(b->active_connections.load(), 1);
        EXPECT_EQ(b->total_requests.load(), 1);
    }

    EXPECT_EQ(b->active_connections.load(), 0);
    EXPECT_EQ(b->total_requests.load(), 1);
}
