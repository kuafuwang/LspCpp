#pragma once
#include <LibLsp/lsp/asio.h>
#include <iostream>

namespace lsp
{
class ProcessIoService
{
public:
    using IOService = asio::io_context;
    using Work = asio::executor_work_guard<asio::io_context::executor_type>;
    using WorkPtr = std::unique_ptr<Work>;

    ProcessIoService()
    {

        work_ = std::unique_ptr<Work>(new Work(ioService_.get_executor()));
        auto temp_thread_ = new std::thread([this] { ioService_.run(); });
        thread_ = std::unique_ptr<std::thread>(temp_thread_);
    }

    ProcessIoService(ProcessIoService const&) = delete;
    ProcessIoService& operator=(ProcessIoService const&) = delete;

    asio::io_context& getIOService()
    {
        return ioService_;
    }

    void stop()
    {

        work_.reset();

        thread_->join();
    }

private:
    IOService ioService_;
    WorkPtr work_;
    std::unique_ptr<std::thread> thread_;
};

} // namespace lsp
