export class XunError extends Error {
  line: number;
}

export class Tagged {
  tag: string;
  value: string;
  constructor(tag: string, value: string);
}

export function parse(source: string): Record<string, any>;
export function encode(value: Record<string, any>): string;
export function stringify(value: Record<string, any>): string;
