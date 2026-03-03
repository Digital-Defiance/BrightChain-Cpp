#pragma once

#include <stdexcept>
#include <string>

namespace brightchain::db {

/**
 * Error codes for database operations.
 * Values are chosen to align with MongoDB-style error codes.
 */
enum class ErrorCode : int {
    DocumentNotFound = 404,
    ValidationError = 121,
    DuplicateKey = 11000,
    IndexError = 86,
    RegistryError = 500
};

/**
 * Base error class for all database errors.
 * Stores an ErrorCode alongside the human-readable message.
 */
class DbError : public std::runtime_error {
public:
    inline DbError(ErrorCode code, const std::string& message)
        : std::runtime_error(message), code_(code) {}

    inline ErrorCode code() const { return code_; }

private:
    ErrorCode code_;
};

/**
 * Thrown when a document cannot be found by its ID.
 */
class DocumentNotFoundError : public DbError {
public:
    inline explicit DocumentNotFoundError(const std::string& message)
        : DbError(ErrorCode::DocumentNotFound, message) {}
};

/**
 * Thrown when a document or operation fails validation.
 */
class ValidationError : public DbError {
public:
    inline explicit ValidationError(const std::string& message)
        : DbError(ErrorCode::ValidationError, message) {}
};

/**
 * Thrown when an insert would create a duplicate key.
 */
class DuplicateKeyError : public DbError {
public:
    inline explicit DuplicateKeyError(const std::string& message)
        : DbError(ErrorCode::DuplicateKey, message) {}
};

/**
 * Thrown on index-related errors.
 */
class IndexError : public DbError {
public:
    inline explicit IndexError(const std::string& message)
        : DbError(ErrorCode::IndexError, message) {}
};

/**
 * Thrown on head registry or store-level errors.
 */
class RegistryError : public DbError {
public:
    inline explicit RegistryError(const std::string& message)
        : DbError(ErrorCode::RegistryError, message) {}
};

} // namespace brightchain::db
