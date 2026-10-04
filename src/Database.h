#pragma once

#include <libpq-fe.h>

#include <condition_variable>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <utility>
#include <vector>

class Database {
public:
    using Connection =
        std::unique_ptr<PGconn, decltype(&PQfinish)>;

    using Task = std::function<void(Connection&)>;

    explicit Database(std::string url)
        : url_(std::move(url))
    {
        for (int i = 0; i < 4; ++i) {
            workers_.emplace_back([this] {
                Connection connection(nullptr, PQfinish);

                for (;;) {
                    Task task;

                    {
                        std::unique_lock lock(mutex_);

                        ready_.wait(lock, [this] {
                            return stopping_ || !tasks_.empty();
                        });

                        if (stopping_ && tasks_.empty()) {
                            return;
                        }

                        task = std::move(tasks_.front());
                        tasks_.pop();
                    }

                    task(connection);
                }
            });
        }
    }

    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    ~Database()
    {
        {
            std::lock_guard lock(mutex_);
            stopping_ = true;
        }

        ready_.notify_all();

        for (auto& worker : workers_) {
            if (worker.joinable()) {
                worker.join();
            }
        }
    }

    bool submit(Task task)
    {
        {
            std::lock_guard lock(mutex_);

            if (stopping_ || tasks_.size() >= 128) {
                return false;
            }

            tasks_.push(std::move(task));
        }

        ready_.notify_one();
        return true;
    }

    bool connect(Connection& connection) const
    {
        if (connection &&
            PQstatus(connection.get()) == CONNECTION_OK) {
            return true;
        }

    const char* keys[] = {
        "dbname",
        "connect_timeout",
        nullptr
    };

    const char* values[] = {
        url_.c_str(),
        "10",
        nullptr
    };

        connection.reset(
            PQconnectdbParams(keys, values, 1)
        );

        if (!connection) {
            std::cerr
                << "PostgreSQL connection allocation failed\n";
            return false;
        }

        if (PQstatus(connection.get()) != CONNECTION_OK) {
            std::cerr
                << "PostgreSQL connection failed: "
                << PQerrorMessage(connection.get())
                << '\n';

            return false;
        }

        std::cerr << "PostgreSQL connection established\n";
        return true;
    }

private:
    std::string url_;

    std::mutex mutex_;
    std::condition_variable ready_;

    std::queue<Task> tasks_;
    std::vector<std::thread> workers_;

    bool stopping_ = false;
};