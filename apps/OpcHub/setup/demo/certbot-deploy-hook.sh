#!/bin/sh
# certbot's deploy hook for the public demo, /etc/letsencrypt/renewal-hooks/deploy/jde-demo, run after each renewal:  nginx
# reads a certificate when it loads its config, so a renewed one is served only once nginx reloads.
systemctl reload nginx