# frpc
*An rpc code generation framework for c++, It supports c++17/20/23*

[![](https://github.com/fantasy-peak/frpc/workflows/ubuntu-gcc13/badge.svg)](https://github.com/fantasy-peak/frpc/actions) 

## Support this repository:

-   ⭐ **Star the project:** Star this. It means a lot to me! 💕

## Getting Started :white_check_mark:
To get started with this project, you'll need to clone the repository and have g++ >= 13.1 installed on your system.

## Compile And Running the Application :rocket:
To run the application, run the following command:

```
1. Check local g++ version, need g++ version >= gcc version 13.1.0 (GCC)
2. git clone https://github.com/fantasy-peak/frpc.git
3. xmake build -v -y
4. xmake install -o .
5. ./bin/frpc -f ./config/config.yaml -t ./template/cpp -o ./out
// compile and run test
6. xmake build -v -y --file=./test_xmake.lua
7. xmake install -o . --file=./test_xmake.lua
8. ./bin/cpp23
```

## Type Mapping
```
{"bool", "bool"},
{"i8", "int8_t"},
{"u8", "uint8_t"},
{"i16", "int16_t"},
{"u16", "uint16_t"},
{"i32", "int32_t"},
{"u32", "uint32_t"},
{"i64", "int64_t"},
{"u64", "uint64_t"},
{"f32", "float"},
{"f64", "double"},
{"string", "std::string"},
{"map", "std::unordered_map"},
{"vector", "std::vector"},
```

## Protocol Definition
It uses yaml to define interface data, Then generate a header file library.

```
property:
  filename: frpc.hpp  # Generate the name of the header file library
  namespace: frpc     # c++ namespace name

Info:
  type: struct
  definitions:
    Name: { type: string, comment: "c++ std::string type" }

BankInfo:
  type: struct
  definitions:
    TestMap: { type: map<bool;u32>, comment: "c++ std::unordered_map type" }
    TestVector: { type: vector<string>, comment: "c++ std::vector type" }

HelloWorldApi:
  type: interface
  pattern: bi
  caller: HelloWorldClient
  callee: HelloWorldServer
  definitions:
    hello_world:
      inputs:
        bank_info: {type: BankInfo}
        blance: {type: u64}
      outputs:
        reply: {type: string}
        info: {type: Info}
        count: {type: u64}

UniInterface:
  type: interface
  pattern: uni
  caller: HelloWorldSender
  callee: HelloWorldReceiver
  definitions:
    hello_world:
      inputs:
        in: {type: string}
    notice:
      inputs:
        in: {type: i32}
        info: {type: string}

Stream:
  type: interface
  pattern: bi_stream
  caller: StreamClient
  callee: StreamServer
  definitions:
    hello_world:
      inputs:
        bank_name: {type: string}
      outputs:
        reply: {type: string}
```

## c++17 callback example
### bi (zmq router and zmq dealer) frpc server example

```
frpc::ChannelConfig bi_config{};
bi_config.addr = "tcp://127.0.0.1:5878";
auto server = frpc::HelloWorldServer::create(
    bi_config,
    std::make_shared<Handler>(),
    [](std::string error) {
        spdlog::error("frpc::HelloWorldServer error: {}", error);
    });
server->start();
```

### bi (zmq router and zmq dealer) frpc client example
```
frpc::ChannelConfig bi_config{};
bi_config.addr = "tcp://127.0.0.1:5878";
auto client = frpc::HelloWorldClient::create(bi_config, [](std::string error) {
    spdlog::error("frpc::HelloWorldClient error: {}", error);
});
client->start();

std::string bank_name = "zhongxin";
client->hello_world(
    create_bank_info(),
    bank_name,
    999,
    [](std::string reply, frpc::Info info, uint64_t count) {
        spdlog::info("frpc::HelloWorldClient::hello_world recv: {},{},{}", reply, frpc::toString(info), count);
    });

// set timeout 200 milliseconds
client->hello_world(
    create_bank_info(),
    bank_name,
    999,
    [](std::string reply, frpc::Info info, uint64_t count) {
        spdlog::info("frpc::HelloWorldClient::hello_world(timeout) recv: {},{},{}", reply, frpc::toString(info), count);
    },
    std::chrono::milliseconds(200),
    [] {
        spdlog::info("frpc::HelloWorldClient::timeout timeout!!!");
    });
```

### uni (zmq pub and zmq sub) frpc server example
```
struct HelloWorldReceiverHandler : public frpc::HelloWorldReceiverHandler {
    virtual void hello_world(std::string in) override {
        spdlog::info("HelloWorldReceiverHandler::hello_world: {}", in);
        return;
    }
    virtual void notice(int32_t in, std::string info) override {
        spdlog::info("HelloWorldReceiverHandler::notice: {}: {}", in, info);
        return;
    }
};
frpc::ChannelConfig sub_config{};
sub_config.addr = "tcp://127.0.0.1:5877";
auto receiver = frpc::HelloWorldReceiver::create(
    sub_config,
    std::make_shared<HelloWorldReceiverHandler>(),
    [](auto error) {
        spdlog::error("{}", error);
    });
receiver->start();
```

### uni (zmq pub and zmq sub) frpc clinet example
```
frpc::ChannelConfig pub_config{};
pub_config.addr = "tcp://127.0.0.1:5877";
auto sender = frpc::HelloWorldSender::create(pub_config);
int i = 10;
while (i--) {
    sender->hello_world(std::to_string(i) + "_frpc");
    sender->notice(i, "hello world");
    std::this_thread::sleep_for(std::chrono::seconds(1));
}
```



## c++20 coroutine example(It relies on independent asio)
### bi (zmq router and zmq dealer) frpc server example

```
struct CoroHandler : public frpc::CoroHelloWorldServerHandler {
    virtual asio::awaitable<void> hello_world(
        frpc::BankInfo bank_info, 
        std::string bank_name,
        uint64_t blance,
        std::function<void(std::string, frpc::Info, uint64_t)> cb) override {
        spdlog::info("coro frpc::HelloWorldServer server recv: {}, bank_name: {}, blance: {}",
                     frpc::toString(bank_info), bank_name, blance);
        frpc::Info info;
        info.name = "coro test";
        cb("coro hello world", std::move(info), 556);
        co_return;
    }
};
frpc::ChannelConfig bi_config{};
bi_config.addr = "tcp://127.0.0.1:5879";
auto server = frpc::HelloWorldServer::create(
    bi_config,
    std::make_shared<CoroHandler>(),
    [](std::string error) {
        spdlog::error("frpc::HelloWorldServer error: {}", error);
    });
server->start();
```

### bi (zmq router and zmq dealer) frpc client example
```
asio::co_spawn(pool.getIoContext(), [&]() -> asio::awaitable<void> {
    std::cout << std::this_thread::get_id() << std::endl;
    frpc::ChannelConfig bi_config{};
    bi_config.addr = "tcp://127.0.0.1:5879";
    auto client = frpc::HelloWorldClient::create(bi_config, [](std::string error) {
        spdlog::error("coro frpc::HelloWorldClient error: {}", error);
    });
    client->start();
    spdlog::info("start coro client.");

    auto [reply, info, count] = co_await client->hello_world_coro(create_bank_info(), "HF", 777, asio::as_tuple(asio::use_awaitable));
    spdlog::info("coro frpc::HelloWorldClient::hello_world recv: {},{},{}", reply, frpc::toString(info), count);
    // set timeout
    auto ret = co_await client->hello_world_coro(create_bank_info(), "HF", 666, std::chrono::milliseconds{500}, asio::use_awaitable);
    if (ret.has_value()) {
        auto [reply, info, count] = ret.value();
        spdlog::info("coro frpc::HelloWorldClient::hello_world recv: {},{},{}", reply, frpc::toString(info), count);
        co_return;
    }
    spdlog::error("coro frpc::HelloWorldClient::hello_world timeout");
    co_return;
}, asio::detached);
```

### uni (zmq pub and zmq sub) frpc server example
```
struct CoroHelloWorldReceiver : public frpc::CoroHelloWorldReceiverHandler {
    virtual asio::awaitable<void> hello_world(std::string in) override {
        spdlog::info("CoroHelloWorldReceiver::hello_world: {}", in);
        co_return;
    }
    virtual asio::awaitable<void> notice(int32_t in, std::string info) override {
        spdlog::info("CoroHelloWorldReceiver::notice: {}: {}", in, info);
        co_return;
    }
};
frpc::ChannelConfig sub_config{};
sub_config.addr = "tcp://127.0.0.1:5877";
auto receiver = frpc::HelloWorldReceiver::create(
    sub_config,
    std::make_shared<CoroHelloWorldReceiver>(),
    [](auto error) {
        spdlog::error("{}", error);
    });
receiver->start();
```
