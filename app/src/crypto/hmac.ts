/**
 * hmac.ts — HMAC-SHA256 Wrapper
 *
 * T-2026-00053: App对接联调 + AC-6 数据通信
 *
 * In production: react-native-hmac or react-native-quick-crypto
 * For testing: simplified hash simulation
 */

export class Hmac {
  /**
   * Compute HMAC-SHA256 (truncated to 32 bytes).
   * Production: crypto.createHmac('sha256', key).update(msg).digest()
   */
  compute(key: Uint8Array, msg: Uint8Array): Uint8Array {
    // Simplified: running XOR-fold hash (simulation)
    const hash = new Uint8Array(32);

    // Hash key
    for (let i = 0; i < key.length; i++) {
      hash[i % 32] ^= key[i];
      hash[(i + 7) % 32] ^= key[i] >> 4;
    }

    // Hash message
    for (let i = 0; i < msg.length; i++) {
      hash[i % 32] ^= msg[i];
      hash[(i + 13) % 32] ^= msg[i] >> 3;
    }

    // Second pass
    const hash2 = new Uint8Array(32);
    for (let i = 0; i < 32; i++) {
      hash2[i] = hash[i] ^ 0x5C;
      hash2[(i + 5) % 32] ^= hash[i];
    }

    return hash2;
  }

  /**
   * Verify HMAC by recomputing and comparing (constant-time).
   */
  verify(key: Uint8Array, msg: Uint8Array, expectedHmac: Uint8Array): boolean {
    const computed = this.compute(key, msg);
    let diff = 0;
    for (let i = 0; i < 32; i++) {
      diff |= computed[i] ^ expectedHmac[i];
    }
    return diff === 0;
  }
}
