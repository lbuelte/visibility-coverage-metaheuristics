# nginx with HTTPS termination for the GIS CUP 2026 Solution Viewer.

FROM nginx:1.30-alpine

# openssl creates the temporary self-signed certificate used until Let's
# Encrypt has issued the real one.
RUN apk add --no-cache openssl

COPY default.conf.template /etc/nginx/templates/default.conf.template
COPY --chmod=755 40-tls-certificates.sh /docker-entrypoint.d/40-tls-certificates.sh
