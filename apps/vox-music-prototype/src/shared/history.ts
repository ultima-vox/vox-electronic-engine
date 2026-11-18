import { useCallback, useRef, useState } from 'react';

/** Immutable snapshots only. Musical history never contains visual layout or transport state. */
export function useHistory<T>(initial: () => T, limit = 60) {
  const [value, setValue] = useState(initial);
  const current = useRef(value);
  const past = useRef<T[]>([]);
  const future = useRef<T[]>([]);
  const transaction = useRef<{ before: T } | null>(null);
  const [, refresh] = useState(0);
  const publish = useCallback((next: T) => {
    current.current = next;
    setValue(next);
  }, []);
  const remember = useCallback((before: T) => {
    past.current = [...past.current.slice(1 - limit), before];
    future.current = [];
  }, [limit]);
  const end = useCallback(() => {
    const before = transaction.current;
    transaction.current = null;
    if (before && before.before !== current.current) {
      remember(before.before);
      refresh(n => n + 1);
    }
  }, [remember]);
  const begin = useCallback(() => {
    end();
    transaction.current = { before: current.current };
  }, [end]);
  const edit = useCallback((update: (old: T) => T) => {
    const before = current.current;
    const next = update(before);
    if (next === before) return;
    if (!transaction.current) remember(before);
    publish(next);
  }, [publish, remember]);
  const undo = useCallback(() => {
    end();
    if (past.current.length === 0) return;
    future.current.push(current.current);
    publish(past.current.pop() as T);
  }, [publish, end]);
  const redo = useCallback(() => {
    end();
    if (future.current.length === 0) return;
    past.current.push(current.current);
    publish(future.current.pop() as T);
  }, [publish, end]);
  return { value, current, edit, begin, end, undo, redo,
    canUndo: past.current.length > 0, canRedo: future.current.length > 0 };
}
export type History<T> = ReturnType<typeof useHistory<T>>;
