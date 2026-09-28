#pragma once

#include "Process.h"

#include "connection/config/Config.h"

#include "Context.h"

namespace Gadgetron::Server::Connection::Nodes {
    Gadgetron::Process::child start_julia_module(
        const Config::Execute &,
        unsigned short port,
        const Gadgetron::Core::StreamContext &
    );
    bool julia_available() noexcept;
}

