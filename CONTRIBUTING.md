# Contributing

Thanks for helping to improve radiobrowser-cpp!

## Workflow

1. Open an issue for larger changes so that the approach can be discussed first.
2. Fork the repository and create a branch.
3. Make your change with tests. Unit tests must not need network access; use
   the fake transports in `tests/support` and JSON fixtures in `tests/fixtures`.
4. Run the checks locally:

   ```sh
   cmake --preset dev && cmake --build --preset dev && ctest --preset dev
   git ls-files '*.hpp' '*.cpp' | xargs clang-format --dry-run --Werror
   cmake --preset tidy && cmake --build --preset tidy
   ```

5. Open a pull request. Its title must follow
   [Conventional Commits](https://www.conventionalcommits.org/), because pull
   requests are squash-merged and the title becomes the commit message that
   drives versioning:

   - `feat: add support for /json/add` - new feature (minor release)
   - `fix: handle empty tag lists` - bug fix (patch release)
   - `docs: ...`, `test: ...`, `ci: ...`, `refactor: ...`, `chore: ...` - no release
   - `feat!: ...` or a `BREAKING CHANGE:` footer - breaking change

## Guidelines

- Public headers (`include/`) must stay C++17 compatible; CI compiles them as
  C++17. The implementation (`src/`) uses C++20.
- Do not use exceptions across the public API; return `Result<T>`.
- Keep third-party types (nlohmann/json, libcurl) out of the public headers.
- New public functions need documentation comments and tests.
- Follow the existing naming: types `CamelCase`, functions and variables
  `snake_case`, private members with a trailing underscore, constants `kName`.
- Live tests must never count clicks or vote.

## Releases

Maintainers merge the release pull request that release-please keeps up to
date. Merging it bumps the version in `CMakeLists.txt`, updates
`CHANGELOG.md`, tags the release and publishes it on GitHub.
