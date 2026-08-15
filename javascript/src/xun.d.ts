export class XunError extends Error {
  line: number;
  column: number;
  sourceLine: string;
}

export class Tagged {
  tag: string;
  value: string;
  constructor(tag: string, value: string);
  toDate(): Date;
  toBytes(): Uint8Array;
  toBytesSize(): number;
  toDurationSeconds(): number;
  toVersionParts(): number[];
}

export function parseSize(s: string): number;
export function parseDuration(s: string): number;
export function parseVersion(s: string): number[];
export function unpack(v: any): any;

export function decode(source: string): any;
export function parse(source: string): any;
export function encode(value: any): string;
export function stringify(value: any): string;
