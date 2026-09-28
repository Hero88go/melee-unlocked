FROM python:3.12-slim
WORKDIR /app
COPY server.py /app/server.py
RUN useradd --uid 10001 --create-home lobby && mkdir /data && chown lobby /data
USER lobby
EXPOSE 8787/tcp 8787/udp
CMD ["python", "server.py", "--host", "0.0.0.0", "--port", "8787", "--database", "/data/lobby.sqlite3"]
