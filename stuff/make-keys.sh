#!/bin/bash

readonly N_DAYS=365
readonly N_BITS=2048
readonly KEYS_DIR=${0%make-keys.sh}keys

set -x

mkdir -p "$KEYS_DIR"
cd "$KEYS_DIR" || exit 1
rm -f *.pem

openssl req -new -newkey rsa:$N_BITS -nodes -days $N_DAYS -x509 \
    -subj /CN=root \
    -keyout root-key.pem -out root-cert.pem || exit 1

for k in a b; do
    openssl req -new -newkey rsa:$N_BITS -nodes \
        -subj /CN=localhost \
        -keyout $k-key.pem -out $k-csr.pem || exit 1
    openssl x509 -req -CAcreateserial -days $N_DAYS -extfile ../leaf-cert.ext \
        -CAkey root-key.pem -CA root-cert.pem \
        -in $k-csr.pem -out $k-cert.pem || exit 1
    rm -f $k-csr.pem
done

certutil -d sql:$HOME/.pki/nssdb -D -n root
certutil -d sql:$HOME/.pki/nssdb -A -n root -t CT,c,c -i root-cert.pem
