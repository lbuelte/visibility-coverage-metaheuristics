#!/bin/sh
#
# Obtains and renews the Let's Encrypt certificate for $DOMAIN.
#
# Let's Encrypt validates the domain with the HTTP-01 challenge: certbot
# writes a file to the shared webroot, and nginx serves it on port 80.
# Until a certificate exists, issuance is retried every 15 minutes (this
# stays within Let's Encrypt's rate limits). Afterwards, renewal is
# checked twice a day; certbot only renews certificates that expire within
# 30 days. A failed renewal is retried after an hour. nginx picks up new
# certificates by itself.
#
# compose.yaml starts this service only once nginx is healthy. After a
# reboot, Docker restarts all services at once; a challenge that fails
# because nginx is not up yet is simply retried.

set -u

: "${DOMAIN:?DOMAIN must be set}"
: "${LETSENCRYPT_EMAIL:?LETSENCRYPT_EMAIL must be set}"

webroot=/var/www/certbot
certificate="/etc/letsencrypt/live/${DOMAIN}/fullchain.pem"

staging_option=""

if [ "${LETSENCRYPT_STAGING:-0}" = "1" ]; then
    staging_option="--staging"
    echo "Using the Let's Encrypt staging environment (test certificates)."
fi

trap 'exit 0' TERM INT

while :; do
    if [ -s "$certificate" ]; then
        if certbot renew \
            --webroot --webroot-path "$webroot" \
            --no-random-sleep-on-renew \
            --quiet; then
            pause=12h
        else
            echo "Could not renew the certificate for ${DOMAIN};" \
                "retrying in 1 hour."
            pause=1h
        fi
    elif certbot certonly \
        --webroot --webroot-path "$webroot" \
        --domain "$DOMAIN" \
        --email "$LETSENCRYPT_EMAIL" \
        --agree-tos \
        --no-eff-email \
        --non-interactive \
        $staging_option; then
        pause=12h
    else
        echo "Could not obtain a certificate for ${DOMAIN};" \
            "retrying in 15 minutes."
        pause=15m
    fi

    sleep "$pause" &
    wait $!
done
