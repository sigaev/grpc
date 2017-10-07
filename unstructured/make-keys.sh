#!/bin/bash

readonly N_DAYS=365
readonly DIGEST=sha256
readonly KEYS_DIR=${0%make-keys.sh}keys

set -x

mkdir -p "$KEYS_DIR"
cd "$KEYS_DIR" || exit 1
rm -f *.pem

openssl req -new -newkey ec:<(openssl ecparam -name prime256v1) \
    -$DIGEST -nodes -days $N_DAYS -x509 \
    -subj /CN=root \
    -keyout root-key.pem -out root-cert.pem || exit 1

for k in a b; do
    openssl req -new -newkey ec:<(openssl ecparam -name prime256v1) \
        -$DIGEST -nodes \
        -subj /CN=localhost \
        -keyout $k-key.pem -out $k-csr.pem || exit 1
    openssl x509 -req -CAcreateserial -days $N_DAYS -extfile ../leaf-cert.ext \
        -CAkey root-key.pem -CA root-cert.pem -$DIGEST \
        -in $k-csr.pem -out $k-cert.pem || exit 1
    rm -f $k-csr.pem
done

certutil -d sql:$HOME/.pki/nssdb -D -n root
certutil -d sql:$HOME/.pki/nssdb -A -n root -t CT,c,c -i root-cert.pem
