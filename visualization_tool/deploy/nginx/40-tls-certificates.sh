#!/bin/sh
#
# Provides the TLS certificate that nginx uses, at /etc/nginx/tls/.
#
# nginx cannot start its HTTPS server without a certificate, but Let's
# Encrypt only issues one after nginx has answered the challenge on port
# 80. Until the real certificate exists, a temporary self-signed one is
# therefore used. A background loop switches to the Let's Encrypt
# certificate as soon as the certbot service has obtained it, and reloads
# nginx whenever the certificate changes (also after each renewal). A
# reload that fails its configuration check is retried.
#
# The official nginx image runs this script from /docker-entrypoint.d/
# before nginx starts.

set -eu

: "${DOMAIN:?DOMAIN must be set}"

tls_dir=/etc/nginx/tls
self_signed_dir=/etc/nginx/tls-self-signed
lets_encrypt_dir="/etc/letsencrypt/live/${DOMAIN}"

mkdir -p "$tls_dir" "$self_signed_dir"

openssl req -x509 -nodes -newkey rsa:2048 -days 30 \
    -subj "/CN=${DOMAIN}" \
    -keyout "$self_signed_dir/privkey.pem" \
    -out "$self_signed_dir/fullchain.pem" \
    >/dev/null 2>&1


link_certificate() {
    if [ -s "$lets_encrypt_dir/fullchain.pem" ] \
        && [ -s "$lets_encrypt_dir/privkey.pem" ]; then
        source_dir=$lets_encrypt_dir
    else
        source_dir=$self_signed_dir
    fi

    ln -sfn "$source_dir/fullchain.pem" "$tls_dir/fullchain.pem"
    ln -sfn "$source_dir/privkey.pem" "$tls_dir/privkey.pem"
}


certificate_fingerprint() {
    cat "$tls_dir/fullchain.pem" "$tls_dir/privkey.pem" | sha256sum
}


link_certificate

if [ "$(readlink "$tls_dir/fullchain.pem")" = "$self_signed_dir/fullchain.pem" ]; then
    echo "40-tls-certificates.sh: No Let's Encrypt certificate yet;" \
        "using a temporary self-signed certificate."
fi

(
    # A failed check (for example while a link is being replaced) must not
    # end the loop; the next check simply tries again.
    set +e

    current=$(certificate_fingerprint)

    while sleep 60; do
        link_certificate
        latest=$(certificate_fingerprint)

        if [ "$latest" != "$current" ]; then
            # nginx -t fails if certificate and key do not match yet (for
            # example in the middle of a renewal). $current then stays
            # unchanged, so the next check tries the reload again.
            if nginx -t -q && nginx -s reload; then
                current=$latest
                echo "40-tls-certificates.sh: Certificate changed; nginx reloaded."
            else
                echo "40-tls-certificates.sh: Certificate changed, but the" \
                    "configuration check failed; retrying in a minute."
            fi
        fi
    done
) &
