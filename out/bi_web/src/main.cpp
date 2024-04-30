#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <memory>

#include <drogon/drogon.h>
#include <spdlog/spdlog.h>

#include <fantasy.hpp>
#include <string>

using Callback = std::function<void(const drogon::HttpRequestPtr&, std::function<void(const drogon::HttpResponsePtr&)>&&)>;

struct HelloWorldApi final {
    HelloWorldApi(frpc::ChannelConfig bi_config)
        : m_client(fantasy::HelloWorldClient::create(bi_config, [](std::string error) {
            spdlog::error("fantasy::HelloWorldClient error: {}", error);
        })) {
        m_client->start();
    }
    void hello_world(const drogon::HttpRequestPtr& http_request_ptr, std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
        using namespace fantasy;
        auto request = nlohmann::json::parse(http_request_ptr->getBody());
        auto bank_info = request["bank_info"].template get<BankInfo>();
        auto bank_name = request["bank_name"].template get<std::string>();
        auto blance = request["blance"].template get<uint64_t>();
        auto date = request["date"].template get<std::optional<std::string>>();
        static std::chrono::milliseconds timeout(9000);
        m_client->hello_world(
            std::move(bank_info), std::move(bank_name), blance, std::move(date),
            [callback](std::string reply, Info info, uint64_t count, std::optional<std::string> date) mutable {
                nlohmann::json json;
                json["reply"] = std::move(reply);
                json["info"] = std::move(info);
                json["count"] = count;
                json["date"] = std::move(date);
                auto resp = drogon::HttpResponse::newHttpResponse();
                resp->setBody(json.dump());
                callback(resp);
            },
            timeout,
            [callback] {
                auto resp = drogon::HttpResponse::newHttpResponse(
                    drogon::HttpStatusCode::k408RequestTimeout,
                    drogon::ContentType::CT_APPLICATION_JSON);
                callback(resp);
            });
    }
    std::unique_ptr<fantasy::HelloWorldClient> m_client;
};

int main(int argc, char** argv) {
    frpc::ChannelConfig hello_world_api_config{};
    hello_world_api_config.addr = "tcp://127.0.0.1:5878";
    if (char* addr = std::getenv("HELLO_WORLD_API_ADDR"))
        hello_world_api_config.addr = addr;
    HelloWorldApi hello_world_api_client(hello_world_api_config);

    drogon::app().registerHandler(
        "/hello/world",
        [&] -> Callback {
            return std::bind_front(&HelloWorldApi::hello_world, &hello_world_api_client);
        }(),
        {drogon::HttpMethod::Post});

    drogon::app().registerHandler(
        "/interface",
        [&](const drogon::HttpRequestPtr& http_request_ptr, std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            nlohmann::json json;
            using namespace fantasy;
            {
                nlohmann::json tmp;
                tmp["input"]["bank_info"] = BankInfo{};
                tmp["input"]["bank_name"] = std::string{};
                tmp["input"]["blance"] = uint64_t{};
                tmp["input"]["date"] = std::optional<std::string>{};
                tmp["output"]["reply"] = std::string{};
                tmp["output"]["info"] = Info{};
                tmp["output"]["count"] = uint64_t{};
                tmp["output"]["date"] = std::optional<std::string>{};
                json["/hello/world"] = tmp;
            }
            auto resp = drogon::HttpResponse::newHttpResponse();
            resp->setBody(json.dump());
            callback(resp);
        },
        {drogon::HttpMethod::Post});

    drogon::app()
        .loadConfigJson([=] {
            std::ifstream f(std::string{argv[1]});
            Json::Value settings;
            f >> settings;
            if (char* port_ptr = std::getenv("DROGON_PORT"))
                settings["listeners"][0]["port"] = std::atol(port_ptr);
            if (char* threads_num_ptr = std::getenv("DROGON_THREADS_NUM"))
                settings["app"]["number_of_threads"] = std::atol(threads_num_ptr);
            if (char* max_connections_ptr = std::getenv("DROGON_MAX_CONNECTIONS"))
                settings["app"]["max_connections"] = std::atol(max_connections_ptr);
            if (char* level_ptr = std::getenv("DROGON_LOG_LEVEL"))
                settings["app"]["log"]["log_level"] = level_ptr;
            if (char* upload_path = std::getenv("UPLOAD_PATH"))
                settings["app"]["upload_path"] = upload_path;
            if (char* max_connections_per_ip_ptr = std::getenv("DROGON_MAX_CONNECTIONS_PER_IP"))
                settings["app"]["max_connections_per_ip"] = max_connections_per_ip_ptr;
            if (char* client_max_body_size_ptr = std::getenv("CLIENT_MAX_BODY_SIZE"))
                settings["app"]["client_max_body_size"] = client_max_body_size_ptr;
            Json::FastWriter writer;
            std::string json{writer.write(settings)};
            spdlog::info("{}", json);
            return settings;
        }())
        .run();
}
