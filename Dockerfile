FROM python:3.11-slim AS build
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential make pkg-config && rm -rf /var/lib/apt/lists/*
WORKDIR /app
COPY . .
# Build the compressor CLI. Force a clean rebuild and use the Makefile's default targets.
RUN rm -rf obj bin && make all

FROM python:3.11-slim
WORKDIR /app
COPY --from=build /app /app
# Bind to 0.0.0.0 and use platform-provided PORT (Render sets PORT).
ENV HOST=0.0.0.0
ENV PORT=8080
EXPOSE 8080
HEALTHCHECK --interval=30s --timeout=5s --start-period=20s \
  CMD python3 -c "import socket,os; s=socket.socket(); s.settimeout(2); s.connect(('127.0.0.1', int(os.environ.get('PORT','8080')))); s.close()" || exit 1
CMD ["python3", "web/server.py"]
