#include <atomic>
#include <charconv>
#include <chrono>
#include <cstdio>
#include <exception>
#include <limits>
#include <memory>
#include <random>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <vector>

// A Peterson lock supports exactly two participants:
// local participant 0 and local participant 1.
class PetersonLock {
private:
    std::atomic<bool> interested_[2]{{false}, {false}};
    std::atomic<int> turn_{0};

public:
    void acquire(int me) noexcept {
        const int other = 1 - me;

        interested_[me].store(true);
        turn_.store(other);

        while (interested_[other].load() &&
               turn_.load() == other) {
            std::this_thread::yield();
        }
    }

    void release(int me) noexcept {
        interested_[me].store(false);
    }
};

// Combines Peterson locks into a lock for n threads.
class TournamentLock {
private:
    std::size_t leaves_ = 1;
    unsigned depth_ = 0;
    std::unique_ptr<PetersonLock[]> nodes_;

public:
    explicit TournamentLock(std::size_t n) {
        // Round the number of leaves up to a power of two.
        while (leaves_ < n) {
            if (leaves_ >
                std::numeric_limits<std::size_t>::max() / 2) {
                throw std::length_error(
                    "Tournament tree is too large"
                );
            }

            leaves_ *= 2;
            ++depth_;
        }

        // Root is index 1. Index 0 is unused.
        nodes_ = std::make_unique<PetersonLock[]>(leaves_);
    }

    // id is the philosopher's global ID.
    void acquire(std::size_t id) noexcept {
        std::size_t child = leaves_ + id;

        // Acquire locks from the leaf toward the root.
        while (child > 1) {
            const std::size_t parent = child / 2;
            const int side = static_cast<int>(child % 2);

            nodes_[parent].acquire(side);
            child = parent;
        }
    }

    void release(std::size_t id) noexcept {
        const std::size_t leaf = leaves_ + id;

        // Release locks from the root toward the leaf.
        for (unsigned shift = depth_; shift > 0; --shift) {
            const std::size_t parent = leaf >> shift;

            const int side = static_cast<int>(
                (leaf >> (shift - 1)) & 1
            );

            nodes_[parent].release(side);
        }
    }
};

// Acquires a lock when created and releases it when its scope ends.
// This also releases the lock if an exception occurs.
template<class Lock, class Id>
class Guard {
private:
    Lock& lock_;
    Id id_;

public:
    Guard(Lock& lock, Id id) noexcept
        : lock_(lock), id_(id) {
        lock_.acquire(id_);
    }

    ~Guard() {
        lock_.release(id_);
    }

    Guard(const Guard&) = delete;
    Guard& operator=(const Guard&) = delete;
};

void printMarker(const char* edge, const char* mode) {
    if (std::printf("%s %s\n", edge, mode) < 0) {
        throw std::runtime_error("Could not write output");
    }
}

void printEvent(
    const char* mode,
    std::size_t id,
    int round,
    const char* name
) {
    char record[128];

    const int count = std::snprintf(
        record,
        sizeof record,
        "%s %zu %d %s\n",
        mode,
        id,
        round,
        name
    );

    if (count < 0 ||
        static_cast<std::size_t>(count) >= sizeof record) {
        throw std::runtime_error("Could not format event");
    }

    // Print the entire prepared record in one operation.
    if (std::printf("%s", record) < 0) {
        throw std::runtime_error("Could not write event");
    }
}

void runSimulation(std::size_t n, bool coarse) {
    const char* mode = coarse ? "COARSE" : "FINE";

    auto tree = coarse
        ? std::make_unique<TournamentLock>(n)
        : nullptr;

    auto chopsticks = coarse
        ? nullptr
        : std::make_unique<PetersonLock[]>(n);

    std::vector<std::thread> workers;
    workers.reserve(n);

    // Each worker writes only to its own error slot.
    // Main reads the slots after joining all workers.
    std::vector<std::exception_ptr> errors(n);

    const auto seed = static_cast<unsigned long long>(
        std::chrono::steady_clock::now()
            .time_since_epoch()
            .count()
    );

    printMarker("BEGIN", mode);

    try {
        for (std::size_t id = 0; id < n; ++id) {
            workers.emplace_back([&, id] {
                try {
                    const auto wideId =
                        static_cast<unsigned long long>(id);

                    // Every thread has its own random engine.
                    std::seed_seq seeds{
                        static_cast<unsigned>(seed),
                        static_cast<unsigned>(seed >> 32),
                        static_cast<unsigned>(wideId),
                        static_cast<unsigned>(wideId >> 32)
                    };

                    std::mt19937 generator(seeds);
                    std::uniform_int_distribution<int>
                        delay(1, 500);

                    auto pause = [&] {
                        std::this_thread::sleep_for(
                            std::chrono::milliseconds(
                                delay(generator)
                            )
                        );
                    };

                    auto eat = [&](int round) {
                        printEvent(
                            mode, id, round, "EAT_START"
                        );

                        pause();

                        printEvent(
                            mode, id, round, "EAT_END"
                        );
                    };

                    for (int round = 0; round < 5; ++round) {
                        printEvent(
                            mode, id, round, "THINK_START"
                        );

                        pause();

                        printEvent(
                            mode, id, round, "THINK_END"
                        );

                        if (coarse) {
                            // One global lock protects all
                            // chopsticks throughout eating.
                            Guard<TournamentLock, std::size_t>
                                hold(*tree, id);

                            eat(round);
                        } else {
                            // Chopstick k lies between
                            // philosophers k and (k+1)%n.
                            // Philosopher id therefore needs
                            // chopsticks id and (id-1+n)%n.
                            const std::size_t previous =
                                id == 0 ? n - 1 : id - 1;

                            // Always acquire the lower-numbered
                            // chopstick first to prevent deadlock.
                            const std::size_t first =
                                previous < id ? previous : id;

                            const std::size_t second =
                                previous < id ? id : previous;

                            // On chopstick k:
                            // philosopher k uses local ID 0;
                            // philosopher (k+1)%n uses local ID 1.
                            Guard<PetersonLock, int> firstLock(
                                chopsticks[first],
                                id == first ? 0 : 1
                            );

                            Guard<PetersonLock, int> secondLock(
                                chopsticks[second],
                                id == second ? 0 : 1
                            );

                            eat(round);
                        }
                    }
                } catch (...) {
                    errors[id] = std::current_exception();
                }
            });
        }
    } catch (...) {
        // If thread creation fails, finish and join all
        // threads that were successfully created.
        for (auto& worker : workers) {
            worker.join();
        }

        throw;
    }

    for (auto& worker : workers) {
        worker.join();
    }

    for (const auto& error : errors) {
        if (error) {
            std::rethrow_exception(error);
        }
    }

    printMarker("END", mode);
}

std::size_t parseCount(int argc, char** argv) {
    if (argc != 2) {
        throw std::invalid_argument(
            "Usage: ./my-dining n (n >= 2)"
        );
    }

    const std::string_view argument(argv[1]);

    if (argument.empty()) {
        throw std::invalid_argument(
            "n must be an integer >= 2"
        );
    }

    for (char character : argument) {
        if (character < '0' || character > '9') {
            throw std::invalid_argument(
                "n must contain decimal digits only"
            );
        }
    }

    std::size_t n = 0;

    const auto result = std::from_chars(
        argument.data(),
        argument.data() + argument.size(),
        n
    );

    if (result.ec != std::errc{} ||
        result.ptr != argument.data() + argument.size() ||
        n < 2) {
        throw std::invalid_argument(
            "n must be a representable integer >= 2"
        );
    }

    return n;
}

int main(int argc, char** argv) {
    try {
        const std::size_t n = parseCount(argc, argv);

        runSimulation(n, true);
        runSimulation(n, false);

        if (std::fflush(stdout) != 0) {
            throw std::runtime_error(
                "Could not flush output"
            );
        }

        return 0;
    } catch (const std::bad_alloc&) {
        std::fprintf(
            stderr,
            "Error: insufficient memory for the simulation.\n"
        );
    } catch (const std::exception& error) {
        std::fprintf(stderr, "Error: %s\n", error.what());
    } catch (...) {
        std::fprintf(
            stderr,
            "Error: unexpected simulation failure.\n"
        );
    }

    return 1;
}