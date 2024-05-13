#include <zmq.hpp>
#include <string>
#include <iostream>
#include <unistd.h>
#include <thread>

void publisher1() {
    zmq::context_t context(1);
    zmq::socket_t publisher(context, ZMQ_PUB);
    publisher.bind("tcp://*:5555");

    while (true) {
        std::string message = "Hello from publisher 1!";
        zmq::message_t zmq_message(message.size());
        memcpy(zmq_message.data(), message.c_str(), message.size());
        publisher.send(zmq_message);
        std::cout << "Published message: " << message << std::endl;
        sleep(1);
    }
}

void publisher2() {
    zmq::context_t context(1);
    zmq::socket_t publisher(context, ZMQ_PUB);
    publisher.bind("tcp://*:5556");

    while (true) {
        std::string message = "Hello from publisher 2!";
        zmq::message_t zmq_message(message.size());
        memcpy(zmq_message.data(), message.c_str(), message.size());
        publisher.send(zmq_message);
        std::cout << "Published message: " << message << std::endl;
        sleep(2);
    }
}

void subscriber() {
    zmq::context_t context(1);
    zmq::socket_t subscriber(context, ZMQ_SUB);
    subscriber.connect("tcp://localhost:5555");
    subscriber.connect("tcp://localhost:5556");
    subscriber.setsockopt(ZMQ_SUBSCRIBE, "", 0);

    while (true) {
        zmq::message_t message;
        if (subscriber.recv(&message)) {
            std::string msg = std::string(static_cast<char*>(message.data()), message.size());
            std::cout << "Subscriber received message: " << msg << std::endl;
        }
    }
}

int main() {
    std::thread pub1_thread(publisher1);
    std::thread pub2_thread(publisher2);
    std::thread sub_thread(subscriber);

    pub1_thread.join();
    pub2_thread.join();
    sub_thread.join();

    return 0;
}
