# Docker Deployment Guide

## 1. Building the Docker Image
```bash
docker build -t cpp-load-balancer:latest .
```

## 2. Running with Docker Compose
The included `docker-compose.yml` launches the load balancer alongside 3 upstream Python microservices:
```bash
docker compose up -d --build
```

Verify container status:
```bash
docker compose ps
```

Scrape health and telemetry:
```bash
curl http://localhost:8080/health
curl http://localhost:8080/backends
curl http://localhost:8080/metrics
```

Stop the cluster:
```bash
docker compose down
```
