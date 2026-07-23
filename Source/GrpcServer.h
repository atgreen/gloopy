#pragma once

#include <memory>
#include <string>

class MainComponent;

/** Runs the gRPC command-surface server on a background thread and dispatches
    into MainComponent's api* methods. gRPC/protobuf headers are kept in the .cpp
    (pImpl) so the rest of the app doesn't pull them in. */
class GrpcServer
{
public:
    explicit GrpcServer (MainComponent& owner);
    ~GrpcServer();

    bool start (int port);   // binds 127.0.0.1:<port>
    void stop();

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
