#pragma once

#include "balancer/load_balancer.h"
#include <shared_mutex>

namespace balancer {

class RoundRobinBalancer : public ILoadBalancer {
public:
    RoundRobinBalancer() = default;
    explicit RoundRobinBalancer(std::vector<std::shared_ptr<BackendServer>> backends);

    void add_backend(std::shared_ptr<BackendServer> backend) override;
    void set_backends(std::vector<std::shared_ptr<BackendServer>> backends) override;
    [[nodiscard]] std::shared_ptr<BackendServer> select_backend(const http::HttpRequest& req) override;
    [[nodiscard]] std::vector<std::shared_ptr<BackendServer>> get_backends() const override;
    [[nodiscard]] std::string get_name() const override { return "Round Robin"; }

private:
    mutable std::shared_mutex mutex_;
    std::vector<std::shared_ptr<BackendServer>> backends_;
    std::atomic<size_t> index_{0};
};

} // namespace balancer
