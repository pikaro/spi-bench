FROM debian:latest

RUN apt-get update && apt-get install -y --no-install-recommends \
    pahole && \
    apt-get autoremove -y && apt-get clean -y && rm -rf /var/lib/apt/lists/*

WORKDIR /repo
ENTRYPOINT ["pahole"]
