/**
 * aes.ts — AES-128 Crypto Wrapper
 *
 * T-2026-00053: App对接联调 + AC-6 数据通信
 *
 * In production: react-native-aes-crypto or react-native-quick-crypto
 * For testing: simple XOR-based simulation (demonstrates API contract)
 */

export class AesCrypto {
  /**
   * AES-128 encrypt plaintext bytes with the given key.
   * Production: use AES-128-CCM via react-native-aes-crypto
   */
  encrypt(key: Uint8Array, plaintext: number[]): number[] {
    if (key.length !== 16) {
      throw new Error('AES-128 key must be 16 bytes');
    }
    return plaintext.map((byte, i) => byte ^ key[i % 16]);
  }

  /**
   * AES-128 decrypt ciphertext bytes with the given key.
   * Symmetric: same operation as encrypt for XOR-based simulation.
   * Production: AES-128-CCM decrypt via react-native-aes-crypto
   */
  decrypt(key: Uint8Array, ciphertext: number[]): number[] {
    if (key.length !== 16) {
      throw new Error('AES-128 key must be 16 bytes');
    }
    return ciphertext.map((byte, i) => byte ^ key[i % 16]);
  }

  /**
   * Encrypt a hex string and return hex output.
   * Production wrapper for react-native-aes-crypto.
   */
  async encryptHex(keyHex: string, plaintextHex: string): Promise<string> {
    // In production: RNSAes.cbc(plaintextHex, keyHex, ivHex)
    const key = this.hexToBytes(keyHex);
    const plaintext = this.hexToBytes(plaintextHex);
    const ciphertext = this.encrypt(key, plaintext);
    return this.bytesToHex(ciphertext);
  }

  /**
   * Decrypt a hex string and return hex output.
   */
  async decryptHex(keyHex: string, ciphertextHex: string): Promise<string> {
    const key = this.hexToBytes(keyHex);
    const ciphertext = this.hexToBytes(ciphertextHex);
    const plaintext = this.decrypt(key, ciphertext);
    return this.bytesToHex(plaintext);
  }

  private hexToBytes(hex: string): Uint8Array {
    const bytes: number[] = [];
    for (let i = 0; i < hex.length; i += 2) {
      bytes.push(parseInt(hex.substring(i, i + 2), 16));
    }
    return new Uint8Array(bytes);
  }

  private bytesToHex(bytes: number[]): string {
    return bytes.map(b => b.toString(16).padStart(2, '0')).join('');
  }
}
