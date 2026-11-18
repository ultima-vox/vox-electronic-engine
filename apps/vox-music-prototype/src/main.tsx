import React, { useEffect, useState } from 'react';
import { createRoot } from 'react-dom/client';
import { AudioLines, Grid3X3, Layers3 } from 'lucide-react';
import '@fontsource-variable/inter/wght.css';
import '@fontsource-variable/space-grotesk/wght.css';
import '@fontsource/ibm-plex-mono/latin-400.css';
import '@fontsource/ibm-plex-mono/latin-500.css';
import '@fontsource/ibm-plex-mono/latin-600.css';
import { Badge, TabPanel, Tabs } from './design-system/ui';
import type { Theme } from './design-system/tokens';
import './design-system/system.css';
import { DrumsPrototype } from './drums-prototype/DrumsPrototype';
import { FxPrototype } from './fx-prototype/FxPrototype';
import { Console } from './design-system-console/Console';
import './app.css';

type Product = 'drums' | 'fx' | 'system';
const products = [
  { value: 'drums' as const, label: <><Grid3X3 size={14} aria-hidden="true"/> VOX Drums</> },
  { value: 'fx' as const, label: <><AudioLines size={14} aria-hidden="true"/> VOX FX</> },
  { value: 'system' as const, label: <><Layers3 size={14} aria-hidden="true"/> Design System</> },
];
function fromUrl(): Product {
  const hash = window.location.hash;
  return hash === '#fx' ? 'fx' : hash === '#system' ? 'system' : 'drums';
}
function App() {
  const [product, setProduct] = useState<Product>(fromUrl);
  const [theme, setTheme] = useState<Theme>('drums');
  useEffect(() => {
    const sync = () => setProduct(fromUrl());
    window.addEventListener('hashchange',sync); return () => window.removeEventListener('hashchange',sync);
  }, []);
  const open = (next: Product) => { if (next === product) return; setProduct(next); window.history.pushState(null, '', `#${next}`); window.scrollTo(0,0); };
  return <div className="suite" data-theme={product === 'system' ? theme : product}>
    <a className="skip-link" href="#product-panel" onClick={event => { event.preventDefault(); document.getElementById('product-panel')?.focus(); }}>Skip to instrument</a>
    <header className="suite-bar"><div className="suite-brand"><img src="/vox-mark.svg" alt="" width={35} height={35} /><span><strong>VOX<span> MUSIC</span></strong><small>INSTRUMENT SUITE</small></span></div>
      <Tabs label="VOX Music products" id="product" choices={products} value={product} onChange={open} />
      <div className="suite-status"><Badge tone="accent" dot>BROWSER DEMO</Badge></div>
    </header>
    <TabPanel id="product" value={product} className="suite-screen">
      <div className="product-screen" hidden={product !== 'drums'}><DrumsPrototype active={product === 'drums'} /></div>
      <div className="product-screen" hidden={product !== 'fx'}><FxPrototype active={product === 'fx'} /></div>
      <div className="product-screen" hidden={product !== 'system'}><Console theme={theme} onTheme={setTheme} /></div>
    </TabPanel>
  </div>;
}
createRoot(document.getElementById('root')!).render(<React.StrictMode><App /></React.StrictMode>);
