import { describe, it, expect } from 'vitest';
import { uint8ArrayToBase64, base64ToUint8Array } from '../utils/codec.ts';

describe('codec utility', () => {
  it('losslessly encodes and decodes empty Uint8Array', () => {
    const original = new Uint8Array(0);
    const encoded = uint8ArrayToBase64(original);
    const decoded = base64ToUint8Array(encoded);
    expect(decoded).toEqual(original);
    expect(encoded).toBe('');
  });

  it('losslessly round-trips arbitrary binary bytes', () => {
    const original = new Uint8Array([0, 1, 2, 127, 128, 254, 255]);
    const encoded = uint8ArrayToBase64(original);
    const decoded = base64ToUint8Array(encoded);
    expect(decoded).toEqual(original);
  });

  it('round-trips UTF-8 encoded text as bytes', () => {
    const text = 'Hello Yjs Collaboration - test multi-byte UTF-8 string [café, naïve, resumé]';
    const original = new TextEncoder().encode(text);
    const encoded = uint8ArrayToBase64(original);
    const decoded = base64ToUint8Array(encoded);
    expect(new TextDecoder().decode(decoded)).toBe(text);
  });
});
