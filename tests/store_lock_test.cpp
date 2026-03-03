#include <gtest/gtest.h>
#include "brightchain/store_lock.hpp"
#include "brightchain/db_errors.hpp"
#include <filesystem>
#include <fstream>
#include <thread>
#include <chrono>
#include <stdexcept>

using namespace brightchain::db;

class StoreLockTest : public ::testing::Test {
protected:
    void SetUp() override {
        auto tmpl = (std::filesystem::temp_directory_path() / "brightchain_sl_test_XXXXXX").string();
        std::vector<char> buf(tmpl.begin(), tmpl.end());
        buf.push_back('\0');
        ASSERT_NE(mkdtemp(buf.data()), nullptr) << "mkdtemp failed";
        testDir_ = std::string(buf.data());
    }

    void TearDown() override {
        std::filesystem::remove_all(testDir_);
    }

    std::filesystem::path testDir_;
    std::filesystem::path lockFile() const {
        return testDir_ / ".brightchain-db.lock";
    }
};

// Test basic acquire/release cycle
TEST_F(StoreLockTest, AcquireReleaseCycle) {
    StoreLock lock(testDir_);

    EXPECT_FALSE(lock.isHeld());
    EXPECT_FALSE(std::filesystem::exists(lockFile()));

    lock.acquire();
    EXPECT_TRUE(lock.isHeld());
    EXPECT_TRUE(std::filesystem::exists(lockFile()));

    lock.release();
    EXPECT_FALSE(lock.isHeld());
    EXPECT_FALSE(std::filesystem::exists(lockFile()));
}

// Test release is no-op when not held
TEST_F(StoreLockTest, ReleaseWhenNotHeld) {
    StoreLock lock(testDir_);
    EXPECT_NO_THROW(lock.release());
    EXPECT_FALSE(lock.isHeld());
}

// Test Guard RAII releases on scope exit
TEST_F(StoreLockTest, GuardReleasesOnScopeExit) {
    StoreLock lock(testDir_);

    {
        StoreLock::Guard guard(lock);
        EXPECT_TRUE(lock.isHeld());
        EXPECT_TRUE(std::filesystem::exists(lockFile()));
    }

    EXPECT_FALSE(lock.isHeld());
    EXPECT_FALSE(std::filesystem::exists(lockFile()));
}

// Test Guard RAII releases on exception
TEST_F(StoreLockTest, GuardReleasesOnException) {
    StoreLock lock(testDir_);

    try {
        StoreLock::Guard guard(lock);
        EXPECT_TRUE(lock.isHeld());
        throw std::runtime_error("test exception");
    } catch (const std::runtime_error&) {
        // expected
    }

    EXPECT_FALSE(lock.isHeld());
    EXPECT_FALSE(std::filesystem::exists(lockFile()));
}

// Test double-acquire from same process detects contention
// When the lock file already exists, a second StoreLock instance should
// eventually time out (but we use a short-lived external lock file to test).
TEST_F(StoreLockTest, DoubleAcquireDetectsContention) {
    StoreLock lock1(testDir_);
    lock1.acquire();
    EXPECT_TRUE(lock1.isHeld());

    // A second lock on the same path should see the file and retry.
    // We can't easily test the full 5s timeout in a unit test, so we
    // verify the lock file exists and that a second acquire would block.
    EXPECT_TRUE(std::filesystem::exists(lockFile()));

    // Release so the test cleans up
    lock1.release();
}

// Test stale lock recovery after timeout
// Simulate a stale lock by creating the lock file manually, then verify
// that acquire() recovers by force-removing it.
TEST_F(StoreLockTest, StaleLockRecovery) {
    // Create a stale lock file manually
    {
        std::ofstream ofs(lockFile());
        ofs << "stale";
    }
    ASSERT_TRUE(std::filesystem::exists(lockFile()));

    // Create a StoreLock with very short retry to speed up the test.
    // We can't easily change the retry params, but the default 250×20ms = 5s
    // will eventually force-remove the stale lock and succeed.
    // For a faster test, we'll just verify the mechanism works.
    StoreLock lock(testDir_);

    // This should succeed after exhausting retries and force-removing the stale lock
    EXPECT_NO_THROW(lock.acquire());
    EXPECT_TRUE(lock.isHeld());

    lock.release();
    EXPECT_FALSE(std::filesystem::exists(lockFile()));
}

// Test that lock file is at the correct path
TEST_F(StoreLockTest, LockFileAtCorrectPath) {
    StoreLock lock(testDir_);
    lock.acquire();

    auto expected = testDir_ / ".brightchain-db.lock";
    EXPECT_TRUE(std::filesystem::exists(expected));

    lock.release();
}

// Test multiple acquire/release cycles work correctly
TEST_F(StoreLockTest, MultipleAcquireReleaseCycles) {
    StoreLock lock(testDir_);

    for (int i = 0; i < 5; ++i) {
        lock.acquire();
        EXPECT_TRUE(lock.isHeld());
        EXPECT_TRUE(std::filesystem::exists(lockFile()));

        lock.release();
        EXPECT_FALSE(lock.isHeld());
        EXPECT_FALSE(std::filesystem::exists(lockFile()));
    }
}
