# Deployment Guide

## 1. System Requirements
- **Host OS:** Linux (Kernel >= 5.4, Ubuntu 22.04 / 24.04 recommended)
- **CPU:** 2+ cores recommended
- **RAM:** 512 MB minimum, 2 GB recommended
- **Network:** 1 Gbps NIC

## 2. Linux Kernel Socket Tuning
For production deployments handling high concurrent connection volumes, configure the following `sysctl` parameters in `/etc/sysctl.conf`:
```ini
# Increase socket listen backlog
net.core.somaxconn = 65535

# Increase ephemeral port range
net.ipv4.ip_local_port_range = 10240 65535

# Enable TCP SYN cookies to mitigate SYN flood attacks
net.ipv4.tcp_syncookies = 1

# Reduce FIN timeout to recycle closed sockets faster
net.ipv4.tcp_fin_timeout = 15

# Increase maximum open files per process
fs.file-max = 2097152
```
Apply using `sudo sysctl -p`.

## 3. Systemd Service Deployment
Create `/etc/systemd/system/load-balancer.service`:
```ini
[Unit]
Description=High-Performance C++ HTTP Reverse Proxy & Load Balancer
After=network.target

[Service]
Type=simple
User=www-data
Group=www-data
ExecStart=/opt/load-balancer/load_balancer_server -c /opt/load-balancer/config.json
Restart=always
RestartSec=3
LimitNOFILE=65535

[Install]
WantedBy=multi-user.target
```
Enable and start the service:
```bash
sudo systemctl daemon-reload
sudo systemctl enable --now load-balancer.service
```
