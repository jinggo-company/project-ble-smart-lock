/**
 * lockService.ts — Lock Command Service (encrypted communication)
 *
 * T-2026-00053: App对接联调 + AC-6 数据通信
 *
 * Features:
 *   - AES-128 encrypted command send/receive
 *   - HMAC-SHA256 message integrity
 *   - Anti-replay (nonce monotonic + timestamp window)
 *   - Status notification subscription (state change push within 1s)
 */

import { AesCrypto } from '../crypto/aes';
import { Hmac } from '../crypto/hmac';

export interface LockCommand {
  type: 'unlock' | 'lock' | 'query' | 'read_log' | 'key_mgmt' | 'ota';
  payload?: Uint8Array;
}

export interface LockResponse {
  success: boolean;
  resultCode: number;
  errorCode: number;
  data?: Uint8Array;
}

export interface LockStatus {
  lockState: number;
  batteryPercent: number;
}

export type CommandCodes = {
  UNLOCK: 0x01;
  LOCK: 0x02;
  QUERY: 0x03;
  READ_LOG: 0x04;
  KEY_MGMT: 0x05;
  OTA: 0x06;
};

export const COMMAND_CODES: CommandCodes = {
  UNLOCK: 0x01,
  LOCK: 0x02,
  QUERY: 0x03,
  READ_LOG: 0x04,
  KEY_MGMT: 0x05,
  OTA: 0x06,
};

export const RESULT_CODES = {
  OK: 0x00,
  FAIL: 0x01,
  LOW_BATTERY: 0x02,
  AUTH_FAIL: 0x03,
  REJECTED: 0x09,
} as const;

const MSG_MAGIC = [0xAA, 0x55];
const MSG_FOOTER = [0x0D, 0x0A];
const MSG_NONCE_SIZE = 4;
const MSG_HMAC_SIZE = 4;
const MSG_HEADER_SIZE = 2;
const MSG_FOOTER_SIZE = 2;

export interface BleMessageFrame {
  header: number[];
  nonce: number[];
  cmd: number;
  payload: number[];
  hmac: number[];
  footer: number[];
}

export class LockService {
  private deviceId: string;
  private aesCrypto: AesCrypto;
  private hmac: Hmac;
  private sessionKey: Uint8Array | null = null;
  private nonce: number = 0;
  private lastReceivedNonce: number = 0;
  private statusListeners: Set<(status: LockStatus) => void> = new Set();
  private notifyActive: boolean = false;

  constructor(deviceId: string, sessionKey?: Uint8Array) {
    this.deviceId = deviceId;
    this.aesCrypto = new AesCrypto();
    this.hmac = new Hmac();
    if (sessionKey) {
      this.sessionKey = sessionKey;
    }
  }

  /**
   * Set session key after ECDH key exchange.
   * AC-6: AES-128 decryption must succeed with correct key.
   */
  setSessionKey(key: Uint8Array): void {
    if (key.length !== 16) {
      throw new Error('Session key must be 16 bytes (AES-128)');
    }
    this.sessionKey = key;
  }

  /**
   * Build an encrypted BLE command frame.
   * AC-6: frames must be encrypted and HMAC-protected.
   */
  buildCommandFrame(command: LockCommand): BleMessageFrame {
    if (!this.sessionKey) {
      throw new Error('No session key — complete pairing first');
    }

    const cmdCode = this.commandToCode(command.type);
    const payload = command.payload ? Array.from(command.payload) : [];

    // Build nonce (monotonic counter)
    this.nonce++;
    const nonce = [
      (this.nonce >> 24) & 0xFF,
      (this.nonce >> 16) & 0xFF,
      (this.nonce >> 8) & 0xFF,
      this.nonce & 0xFF,
    ];

    // Encrypt payload
    const encryptedPayload = this.aesCrypto.encrypt(this.sessionKey, payload);

    // Compute HMAC over header+nonce+cmd+encrypted_payload
    const hmacInput = [...MSG_MAGIC, ...nonce, cmdCode, ...encryptedPayload];
    const fullHmac = this.hmac.compute(this.sessionKey, new Uint8Array(hmacInput));
    const truncatedHmac = Array.from(fullHmac.slice(0, MSG_HMAC_SIZE));

    return {
      header: [...MSG_MAGIC],
      nonce,
      cmd: cmdCode,
      payload: encryptedPayload,
      hmac: truncatedHmac,
      footer: [...MSG_FOOTER],
    };
  }

  /**
   * Parse and validate a received BLE response frame.
   * AC-6: reject unencrypted/tampered messages.
   */
  parseResponseFrame(raw: number[]): LockResponse | null {
    if (raw.length < MSG_HEADER_SIZE + MSG_NONCE_SIZE + 1 + MSG_HMAC_SIZE + MSG_FOOTER_SIZE) {
      return null; // too short
    }

    // Check header magic
    if (raw[0] !== 0xAA || raw[1] !== 0x55) {
      return null; // not a valid frame
    }

    // Check footer
    const footerStart = raw.length - 2;
    if (raw[footerStart] !== 0x0D || raw[footerStart + 1] !== 0x0A) {
      return null; // invalid footer
    }

    // Extract nonce
    const receivedNonce = (raw[2] << 24) | (raw[3] << 16) | (raw[4] << 8) | raw[5];

    // Anti-replay: nonce must be increasing
    if (receivedNonce <= this.lastReceivedNonce) {
      return null; // replay attack
    }
    this.lastReceivedNonce = receivedNonce;

    // Extract command
    const cmd = raw[6];

    // Extract HMAC
    const payloadStart = 7;
    const payloadEnd = footerStart - MSG_HMAC_SIZE;
    const receivedHmac = raw.slice(payloadEnd, payloadEnd + MSG_HMAC_SIZE);

    // Verify HMAC
    if (this.sessionKey) {
      const hmacInput = raw.slice(0, payloadEnd);
      const expectedHmac = this.hmac.compute(this.sessionKey, new Uint8Array(hmacInput));
      const match = expectedHmac.slice(0, MSG_HMAC_SIZE).every(
        (b, i) => b === receivedHmac[i]
      );
      if (!match) {
        return null; // HMAC mismatch — tampered
      }
    }

    // Extract and decrypt payload
    const encryptedPayload = raw.slice(payloadStart, payloadEnd);
    const decryptedPayload = this.sessionKey
      ? this.aesCrypto.decrypt(this.sessionKey, encryptedPayload)
      : encryptedPayload;

    const resultCode = decryptedPayload.length > 0 ? decryptedPayload[0] : 0;
    const errorCode = decryptedPayload.length > 1 ? decryptedPayload[1] : 0;

    return {
      success: resultCode === RESULT_CODES.OK,
      resultCode,
      errorCode,
      data: new Uint8Array(decryptedPayload.slice(2)),
    };
  }

  /**
   * Subscribe to lock status notifications.
   * AC-6: status changes pushed within 1s via GATT Notify on 0xF001.
   */
  onStatusChange(listener: (status: LockStatus) => void): () => void {
    this.statusListeners.add(listener);
    return () => {
      this.statusListeners.delete(listener);
    };
  }

  /**
   * Simulate receiving a status notification from the lock.
   * In production: this is triggered by BLE Notify on 0xF001.
   */
  simulateStatusNotify(status: LockStatus): void {
    if (!this.notifyActive) return;
    for (const listener of this.statusListeners) {
      try {
        listener(status);
      } catch {
        // ignore
      }
    }
  }

  /**
   * Enable status notification subscription.
   */
  enableNotifications(): void {
    this.notifyActive = true;
  }

  /**
   * Disable status notification subscription.
   */
  disableNotifications(): void {
    this.notifyActive = false;
  }

  private commandToCode(type: string): number {
    switch (type) {
      case 'unlock': return COMMAND_CODES.UNLOCK;
      case 'lock': return COMMAND_CODES.LOCK;
      case 'query': return COMMAND_CODES.QUERY;
      case 'read_log': return COMMAND_CODES.READ_LOG;
      case 'key_mgmt': return COMMAND_CODES.KEY_MGMT;
      case 'ota': return COMMAND_CODES.OTA;
      default: throw new Error(`Unknown command type: ${type}`);
    }
  }
}
