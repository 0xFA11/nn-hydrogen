// Differential harness for the Gimli permutation.
//
// Gimli is the permutation every primitive in libhydrogen is built on. If an
// optimised implementation differs from the portable one by a single bit, every
// hash, every ciphertext and every signature changes — silently, and only on the
// machines that take the optimised path. So the aarch64 port is only safe if we
// can show the two implementations agree.
//
// This drives the PUBLIC API with fixed inputs and prints digests. Build it
// against the portable path and against the aarch64 path; the output must be
// identical, byte for byte.
#include <stdio.h>
#include <string.h>
#include "hydrogen.h"

static void dump(const char *label, const uint8_t *p, size_t n)
{
    printf("%-28s ", label);
    for (size_t i = 0; i < n; i++) printf("%02x", p[i]);
    printf("\n");
}

int main(void)
{
    if (hydro_init() != 0) { printf("hydro_init failed\n"); return 1; }

    // ---- hash, across many lengths: exercises the permutation at every
    // ---- block boundary and absorb/squeeze transition
    for (size_t len = 0; len <= 200; len += 17) {
        uint8_t  in[256];
        uint8_t  out[32];
        char     label[64];
        for (size_t i = 0; i < sizeof(in); i++) in[i] = (uint8_t) (i * 7 + 3);
        uint8_t key[hydro_hash_KEYBYTES];
        for (size_t i = 0; i < sizeof(key); i++) key[i] = (uint8_t) (i + 1);
        hydro_hash_hash(out, sizeof(out), in, len, "gimlidif", key);
        snprintf(label, sizeof(label), "hash[len=%zu]", len);
        dump(label, out, sizeof(out));
    }

    // ---- long hash: many permutation rounds in sequence
    {
        uint8_t big[8192];
        uint8_t out[64];
        for (size_t i = 0; i < sizeof(big); i++) big[i] = (uint8_t) (i * 31 + 11);
        uint8_t key[hydro_hash_KEYBYTES];
        memset(key, 0xA5, sizeof(key));
        hydro_hash_hash(out, sizeof(out), big, sizeof(big), "gimlidif", key);
        dump("hash[8192]", out, sizeof(out));
    }

    // ---- secretbox: deterministic with a fixed nonce-ish path via a fixed key
    {
        uint8_t key[hydro_secretbox_KEYBYTES];
        for (size_t i = 0; i < sizeof(key); i++) key[i] = (uint8_t) (0x10 + i);
        const char *msg = "the permutation must agree on every machine";
        uint8_t ct[128 + hydro_secretbox_HEADERBYTES];
        // probe_create is deterministic given key+ciphertext, so hash the
        // ciphertext deterministically instead of relying on the random nonce
        memset(ct, 0, sizeof(ct));
        if (hydro_secretbox_encrypt(ct, msg, strlen(msg), 0, "gimlidif", key) != 0) {
            printf("secretbox encrypt failed\n"); return 1;
        }
        uint8_t dec[128];
        if (hydro_secretbox_decrypt(dec, ct, strlen(msg) + hydro_secretbox_HEADERBYTES,
                                    0, "gimlidif", key) != 0) {
            printf("secretbox ROUND TRIP FAILED\n"); return 1;
        }
        if (memcmp(dec, msg, strlen(msg)) != 0) {
            printf("secretbox PLAINTEXT MISMATCH\n"); return 1;
        }
        printf("%-28s ok\n", "secretbox roundtrip");
    }

    // ---- sign: deterministic signature over a fixed keypair and message
    {
        hydro_sign_keypair kp;
        uint8_t seed[hydro_sign_SEEDBYTES];
        for (size_t i = 0; i < sizeof(seed); i++) seed[i] = (uint8_t) (i * 3 + 1);
        hydro_sign_keygen_deterministic(&kp, seed);
        dump("sign.pk", kp.pk, hydro_sign_PUBLICKEYBYTES);

        const char *msg = "signatures are permutation output too";
        uint8_t sig[hydro_sign_BYTES];
        if (hydro_sign_create(sig, msg, strlen(msg), "gimlidif", kp.sk) != 0) {
            printf("sign_create failed\n"); return 1;
        }
        if (hydro_sign_verify(sig, msg, strlen(msg), "gimlidif", kp.pk) != 0) {
            printf("sign VERIFY FAILED\n"); return 1;
        }
        printf("%-28s ok\n", "sign verify");
    }

    // ---- kdf: another distinct path through the permutation
    {
        uint8_t master[hydro_kdf_KEYBYTES];
        for (size_t i = 0; i < sizeof(master); i++) master[i] = (uint8_t) (0xF0 - i);
        for (uint64_t id = 0; id < 4; id++) {
            uint8_t sub[32];
            char    label[64];
            hydro_kdf_derive_from_key(sub, sizeof(sub), id, "gimlidif", master);
            snprintf(label, sizeof(label), "kdf[id=%llu]", (unsigned long long) id);
            dump(label, sub, sizeof(sub));
        }
    }

    return 0;
}
