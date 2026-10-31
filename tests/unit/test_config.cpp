#include <gtest/gtest.h>
#include "config/config.h"

TEST(ConfigTest, DefaultConfigIsValid) {
    config::ServerConfig cfg = config::ServerConfig::default_config();
    std::string err;
    EXPECT_TRUE(cfg.validate(err)) << "Default config failed validation: " << err;
    EXPECT_EQ(cfg.port, 8080);
    EXPECT_EQ(cfg.worker_threads, 4);
    EXPECT_FALSE(cfg.backends.empty());
}

TEST(ConfigTest, InvalidConfigRejected) {
    config::ServerConfig cfg = config::ServerConfig::default_config();
    std::string err;

    // Zero port
    cfg.port = 0;
    EXPECT_FALSE(cfg.validate(err));

    // Zero workers
    cfg.port = 8080;
    cfg.worker_threads = 0;
    EXPECT_FALSE(cfg.validate(err));

    // Empty backends
    cfg.worker_threads = 4;
    cfg.backends.clear();
    EXPECT_FALSE(cfg.validate(err));

    // Unsupported algorithm
    cfg.backends.push_back({"127.0.0.1", 9001, 1});
    cfg.algorithm = "random_shuffle";
    EXPECT_FALSE(cfg.validate(err));
}

TEST(ConfigTest, JsonSerializationContainsExpectedFields) {
    config::ServerConfig cfg = config::ServerConfig::default_config();
    std::string json = cfg.to_json_string();
    EXPECT_NE(json.find("\"port\": 8080"), std::string::npos);
    EXPECT_NE(json.find("\"algorithm\": \"round_robin\""), std::string::npos);
    EXPECT_NE(json.find("\"backends\""), std::string::npos);
}
