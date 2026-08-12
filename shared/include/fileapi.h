#pragma once

typedef enum _FILE_CREATION_DISPOSITION {
    FILE_CREATE_NEW,        // Create a new file; fail if it already exists.

    FILE_CREATE_ALWAYS,     // Create a new file, or truncate an existing file.

    FILE_OPEN_EXISTING,     // Open an existing file; fail if it does not exist.

    FILE_OPEN_ALWAYS,       // Open an existing file, or create it if missing.

    FILE_TRUNCATE_EXISTING  // Truncate and open an existing file; fail if missing.
} FILE_CREATION_DISPOSITION;
