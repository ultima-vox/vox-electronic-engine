import type { Parameter, ParameterBinding } from '../design-system/parameters';
import type { History } from '../shared/history';
import type { Program } from './model';
import type { FXPreview } from './preview';
export type FXSession = {
  history: History<Program>;
  preview: FXPreview;
  bind: (spec: Parameter, value: number, apply: (value: number) => void, extra?: Partial<ParameterBinding>) => ParameterBinding;
  say: (message: string) => void;
};
