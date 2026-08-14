export class XunError extends Error {
  line: number;
  constructor(message: string, line?: number);
}

export class Tagged {
  tag: string;
  value: string;
  constructor(tag: string, value: string);
}

export type XunValue =
  | string
  | number
  | bigint
  | boolean
  | Uint8Array
  | Tagged
  | XunValue[]
  | { [key: string]: XunValue };

export function parse(source: string): XunValue;
