#pragma once

#include "Process.h"

#include "connection/config/Config.h"

#include "Context.h"

namespace Gadgetron::Server::Connection::Nodes {
    Gadgetron::Process::child start_python_module(
        const Config::Execute &,
        unsigned short port,
        const Gadgetron::Core::StreamContext &
    );
    bool python_available() noexcept;
}

