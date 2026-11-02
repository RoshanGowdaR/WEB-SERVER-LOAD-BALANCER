# Docker & Containerization Verification Report

**Date:** 2026-11-02  
**Verification Status:** **BLOCKED**  
**Blocking Reason:** The Docker CLI / daemon is not integrated into this active WSL2 Ubuntu distro (`The command 'docker' could not be found in this WSL 2 distro`). Docker Desktop is installed on the Windows host, but WSL integration has not been toggled for Ubuntu in the Docker Desktop UI settings.

---

## 1. Audit Finding & Evidence

When executing container validation commands inside the development environment:
```bash
$ docker --version
The command 'docker' could not be found in this WSL 2 distro.
We recommend to activate the WSL integration in Docker Desktop settings.
```

In accordance with strict verification honesty standards, this phase is marked **`BLOCKED`** rather than claiming an unverified pass.

---

## 2. Docker Architecture & Assets in Repository

The project codebase includes production-grade containerization files ready for deployment once Docker WSL integration is enabled:

1. **`docker/Dockerfile`:**
   - Multi-stage build pattern using `ubuntu:24.04`.
   - **Stage 1 (Builder):** Installs `build-essential`, `cmake`, and builds the Release binary (`-DCMAKE_BUILD_TYPE=Release`).
   - **Stage 2 (Runtime):** Minimal image containing only the compiled binary (`load_balancer_server`) and runtime dependencies, running under an unprivileged system user (`loadbalancer:loadbalancer`).
2. **`docker-compose.yml`:**
   - Orchestrates the full architecture:
     - `load_balancer`: Reverse proxy exposed on host port `8080`.
     - `backend1`, `backend2`, `backend3`: Isolated upstream HTTP backend containers on internal docker network `lb-net`.
     - `prometheus`: Scrapes metrics from `http://load_balancer:8080/metrics` every 5 seconds.
     - `grafana`: Pre-configured dashboard on port `3000`.

---

## 3. Step-by-Step Operator Instructions to Run Docker

Once Docker Desktop WSL integration is toggled on the host machine:

### Step 1: Enable WSL Integration in Docker Desktop
1. Open **Docker Desktop** on Windows.
2. Navigate to **Settings** $\to$ **Resources** $\to$ **WSL Integration**.
3. Toggle on **Ubuntu** (the active distro).
4. Click **Apply & Restart**.

### Step 2: Build & Start Containers
Run inside WSL2 Ubuntu terminal:
```bash
cd "c:/Users/gowda/OneDrive/Desktop/Web Server & Load Balancer"
docker compose up --build -d
```

### Step 3: Validate Operational Services
```bash
# 1. Verify all services are running
docker compose ps

# 2. Check Load Balancer Health
curl -s http://localhost:8080/health | jq .

# 3. Check Prometheus Metrics Endpoint
curl -s http://localhost:8080/metrics

# 4. Access Prometheus Web UI
http://localhost:9090

# 5. Access Grafana Dashboard
http://localhost:3000 (admin / admin)
```

### Step 4: Tear Down
```bash
docker compose down -v
```
