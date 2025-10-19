/// <reference types="node" />

import { EventEmitter } from "events";
import TypedEmitter from "typed-emitter";

export function createSocket(
  type: "unix_dgram",
  listener?: (msg: Buffer, rinfo: RemoteInfo) => void,
): Socket;

export interface RemoteInfo {
  size: number;
  address: Record<string, never>;
  path: string;
}

export type SocketEvents = {
  close: () => void;
  connect: () => void;
  error: (err: Error) => void;
  listening: () => void;
  message: (msg: Buffer, rinfo: RemoteInfo) => void;
};

export class Socket
  extends (EventEmitter as new () => TypedEmitter<SocketEvents>) {
  bind(path: string): void;
  connect(path: string): void;
  send(
    buf: Buffer,
    offset: number,
    length: number,
    path: string,
    callback?: (err: Error | null) => void,
  ): void;
  send(buf: Buffer, callback?: (err: Error | null) => void): void;
  sendto(
    buf: Buffer,
    offset: number,
    length: number,
    path: string,
    callback?: (err: Error | null) => void,
  ): void;
  close(): void;
}
