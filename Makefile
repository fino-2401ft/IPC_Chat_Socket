CC = gcc
CFLAGS = -Wall -g -pthread
BINDIR = build
SRCDIR = src
INCLUDEDIR = include

# Target mặc định: clean và build
all: clean $(BINDIR)/socket_server $(BINDIR)/socket_client

$(BINDIR)/socket_server: $(SRCDIR)/socket_server.c $(SRCDIR)/server_utils.c
	@mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) -I$(INCLUDEDIR) $^ -o $@
	@echo "✅ Server built successfully"

$(BINDIR)/socket_client: $(SRCDIR)/socket_client.c $(SRCDIR)/client_utils.c
	@mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) -I$(INCLUDEDIR) $^ -o $@
	@echo "✅ Client built successfully"

# Clean build files
clean:
	@echo "🧹 Cleaning old build files..."
	@rm -rf $(BINDIR)/*
	@echo "✅ Clean completed"

# Chạy server ở background và client ở foreground
run: all
	@echo "🚀 Starting server and client..."
	@echo "📌 Server will run in background (PID will be shown)"
	@echo "📌 Client will run in foreground"
	@echo "⚠️  Press Ctrl+C to stop client, then run 'make stop-server' to stop server"
	@echo ""
	@$(BINDIR)/socket_server & \
	SERVER_PID=$$!; \
	echo "✅ Server started with PID: $$SERVER_PID"; \
	echo "$$SERVER_PID" > $(BINDIR)/.server_pid; \
	sleep 1; \
	$(BINDIR)/socket_client; \
	echo ""; \
	echo "🛑 Client stopped. Server is still running (PID: $$SERVER_PID)"

# Chạy chỉ server
run-server: all
	@echo "🚀 Starting server..."
	@$(BINDIR)/socket_server

# Chạy chỉ client
run-client: all
	@echo "🚀 Starting client..."
	@$(BINDIR)/socket_client

# Dừng server (nếu đang chạy ở background)
stop-server:
	@if [ -f $(BINDIR)/.server_pid ]; then \
		SERVER_PID=$$(cat $(BINDIR)/.server_pid); \
		if kill -0 $$SERVER_PID 2>/dev/null; then \
			kill $$SERVER_PID; \
			echo "🛑 Server (PID: $$SERVER_PID) stopped"; \
		else \
			echo "⚠️  Server (PID: $$SERVER_PID) is not running"; \
		fi; \
		rm -f $(BINDIR)/.server_pid; \
	else \
		echo "⚠️  No server PID file found"; \
	fi

# Rebuild và chạy (clean + build + run)
rebuild: clean all

.PHONY: all clean run run-server run-client stop-server rebuild