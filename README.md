# frpc
*An rpc code generation framework for c++, It supports c++17/20/23*

[![](https://github.com/fantasy-peak/frpc/workflows/ubuntu-gcc13/badge.svg)](https://github.com/fantasy-peak/frpc/actions) [![](https://github.com/fantasy-peak/frpc/workflows/ubuntu-clang19/badge.svg)](https://github.com/fantasy-peak/frpc/actions) [![](https://img.shields.io/badge/language-C%2B%2B23-yellow.svg
)](https://en.wikipedia.org/wiki/C%2B%2B17)

## Support this repository:

-   ⭐ **Star the project:** Star this. It means a lot to me! 💕

## Quick Start :rocket:
* need install uuid msgpack xmake, >= gcc version 13.1.0 (GCC)

#### 1. compile frpc
```
git clone https://github.com/fantasy-peak/frpc.git
cd frpc
xmake build -v -y --file=./xmake.lua
xmake install -o . --file=./xmake.lua
```
#### 2. define protocol
*[protocol example](config/config.yaml)*.

#### 3. generate code
```
./bin/frpc -f ./config/config.yaml -t ./template/cpp -o ./out -w ./template/web --auto_sort=1 --debug=0
```

#### 4. compile test example
```
xmake build -v -y --file=./test_xmake.lua
xmake install -o . --file=./test_xmake.lua
./bin/coro_frpc_bi
```

## Type Mapping
| frpc type    | c++ type           | frpc type    | c++ type           |
|---------|--------------------|---------|--------------------|
| bool    | bool               | f64     | double             |
| i8      | int8_t             | f32     | float              |
| u8      | uint8_t            | vector  | std::vector        |
| i16     | int16_t            | option  | std::optional      |
| u16     | uint16_t           | string  | std::string        |
| i32     | int32_t            | map     | std::unordered_map |
| u32     | uint32_t           | DateTime| frpc::DateTime     |
| i64     | int64_t            | Date    | frpc::Date         |
| u64     | uint64_t           | Time    | frpc::Time         |


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
      web: # Generate web service definition
        path: "/hello/world"
        timeout: "9000"
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

## Example
*[c++17 bi example](test/cpp/bi.cpp)*.

*[c++20 asio coroutine bi example](test/cpp/coro_bi.cpp)*.

*[c++20 frpc coroutine bi example](test/cpp/coro_frpc_bi.cpp)*.

*[c++20 asio coroutine bi stream example](test/cpp/bi_stream.cpp)*.

*[c++17&&c++20 coroutine uni pubsub example](test/cpp/uni_pub_sub.cpp)*.

*[c++17&&c++20 coroutine uni pushpull example](test/cpp/uni_push_pull.cpp)*.

*[bi web server (auto generate)](out/bi_web/src/main.cpp)*.


## Contact
* **Issue** - You are very welcome to post questions to [issues](https://github.com/fantasy-peak/frpc/issues) list.

