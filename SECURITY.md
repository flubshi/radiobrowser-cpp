# Security policy

## Supported versions

Security fixes are made for the latest released version.

## Reporting a vulnerability

Please do not open a public issue for security problems. Report them
privately through GitHub's
[private vulnerability reporting](https://github.com/flubshi/radiobrowser-cpp/security/advisories/new)
("Report a vulnerability" on the Security tab).

Please include the affected version, a description of the issue and, if
possible, steps or a proof of concept to reproduce it. You can expect an
initial answer within a week.

## Scope notes

The library processes data from public, user maintained servers. Relevant
issues include, for example, crashes or memory errors caused by malicious API
responses, TLS verification problems, header or URL injection, and unbounded
resource usage.
