# FetchForge

FetchForge is a C++ command-line file downloader powered by libcurl.

The project is being developed on Linux/WSL and focuses on reliable file downloads, error handling, and clean command-line usage.

## Technologies

- C++17
- libcurl
- G++
- Linux / WSL
- Git and GitHub

## Features

- HTTP and HTTPS URL validation
- Command-line argument handling
- File downloading using libcurl
- Automatic redirect handling
- Connection and download timeouts
- HTTP error detection
- Temporary files for incomplete downloads
- Exception-based error reporting

## Requirements

Install the required packages on Ubuntu/WSL:

```bash
sudo apt update
sudo apt install g++ libcurl4-openssl-dev
