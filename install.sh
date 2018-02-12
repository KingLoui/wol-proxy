#!/bin/bash
set -e

cp wolproxy /usr/bin
cp wolproxy.service /lib/systemd/system/wolproxy.service

if ! getent group wolproxy >/dev/null; then
    addgroup --quiet --system wolproxy
fi

if ! getent passwd wolproxy >/dev/null; then
    adduser --quiet --system wolproxy \
        --ingroup lounge \
        --no-create-home \
        --gecos "System user for Wake on Lan Proxy"
fi

systemctl daemon-reload