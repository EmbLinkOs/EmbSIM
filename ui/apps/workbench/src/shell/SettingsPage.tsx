// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
//
// The Settings page: the workbench's preferences in four categories, each one
// applied at once and kept between sessions (stores/settings.ts). Only
// settings that act on something are offered; the one that waits for the
// next start says so.
import { Button, SegmentedControl, Select, Toggle, useToast } from '@embsim/design';
import { ArrowLeft, Globe, Info, LayoutPanelLeft, Palette, RotateCcw, Timer } from 'lucide-react';
import { useEffect, useMemo, useRef, useState } from 'react';
import { LANGUAGE_NAMES, useT, type MessageKey } from '../i18n';
import { useSession } from '../stores/session';
import { LANGUAGES, useSettings, type Density, type Language, type RateFormat, type TextSize, type TimeUnitPreference } from '../stores/settings';
import { formatRate, formatSimTime } from './simState';

type Category = 'general' | 'appearance' | 'workspace' | 'simulation' | 'about';
const CATEGORIES: { id: Category; icon: React.ReactNode; label: MessageKey }[] = [
  { id: 'general', icon: <Globe aria-hidden="true" />, label: 'settings.cat.general' },
  { id: 'appearance', icon: <Palette aria-hidden="true" />, label: 'settings.cat.appearance' },
  { id: 'workspace', icon: <LayoutPanelLeft aria-hidden="true" />, label: 'settings.cat.workspace' },
  { id: 'simulation', icon: <Timer aria-hidden="true" />, label: 'settings.cat.simulation' },
  { id: 'about', icon: <Info aria-hidden="true" />, label: 'settings.cat.about' },
];

function Row({ id, title, desc, note, children }: { id: string; title: string; desc: string; note?: string; children: React.ReactNode }) {
  return (
    <div className="wb-setting" data-setting={id}>
      <div className="wb-setting__text">
        <div className="wb-setting__title" id={`setting-${id}`}>
          {title}
        </div>
        <p className="wb-setting__desc">{desc}</p>
        {note && <p className="wb-setting__note">{note}</p>}
      </div>
      <div className="wb-setting__control" aria-labelledby={`setting-${id}`}>
        {children}
      </div>
    </div>
  );
}

/** 1.547382916 s, for the time-format preview */
const PREVIEW_TIME = 1_547_382_916_000n;

export function SettingsPage() {
  const t = useT();
  const s = useSettings();
  const engine = useSession((x) => x.engine);
  const protocol = useSession((x) => x.protocol);
  const { toast } = useToast();
  const [category, setCategory] = useState<Category>('general');
  const sections = useRef<Record<string, HTMLElement | null>>({});
  const close = () => s.setSettingsOpen(false);

  useEffect(() => {
    const onKey = (e: KeyboardEvent) => {
      if (e.key === 'Escape' && !(document.activeElement && document.activeElement.closest('[role="listbox"],[role="dialog"]'))) close();
    };
    window.addEventListener('keydown', onKey);
    return () => window.removeEventListener('keydown', onKey);
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  const go = (c: Category) => {
    setCategory(c);
    sections.current[c]?.scrollIntoView({ block: 'start', behavior: 'smooth' });
  };

  const preview = useMemo(() => formatSimTime(PREVIEW_TIME, s.timeUnit, s.timeDecimals), [s.timeUnit, s.timeDecimals]);

  return (
    <div className="wb-settings" data-testid="settings-page">
      <div className="wb-settings__bar">
        <Button variant="ghost" icon={<ArrowLeft />} onClick={close} aria-label={t('settings.back')}>
          {t('settings.back')}
        </Button>
        <h1 className="wb-settings__title">{t('settings.title')}</h1>
        <span className="wb-settings__applied">{t('settings.appliedNow')}</span>
      </div>
      <div className="wb-settings__body">
        <nav className="wb-settings__nav" aria-label={t('settings.categories')}>
          {CATEGORIES.map((c) => (
            <button key={c.id} type="button" className={`wb-settings__navitem${category === c.id ? ' wb-settings__navitem--active' : ''}`} aria-current={category === c.id ? 'true' : undefined} onClick={() => go(c.id)}>
              {c.icon}
              <span>{t(c.label)}</span>
            </button>
          ))}
        </nav>
        <div className="wb-settings__content">
          <section ref={(el) => { sections.current['general'] = el; }} aria-labelledby="sec-general" className="wb-settings__section">
            <h2 id="sec-general">{t('settings.cat.general')}</h2>
            <Row id="language" title={t('settings.language')} desc={t('settings.language.desc')} note={t('settings.language.fallback')}>
              <Select
                label={t('settings.language')}
                value={s.language}
                options={LANGUAGES.map((l) => ({ value: l, label: LANGUAGE_NAMES[l] }))}
                onValueChange={(v) => s.set('language', v as Language)}
              />
            </Row>
          </section>

          <section ref={(el) => { sections.current['appearance'] = el; }} aria-labelledby="sec-appearance" className="wb-settings__section">
            <h2 id="sec-appearance">{t('settings.cat.appearance')}</h2>
            <Row id="theme" title={t('settings.theme')} desc={t('settings.theme.desc')}>
              <SegmentedControl
                label={t('settings.theme')}
                value={s.theme}
                options={(['system', 'light', 'dark'] as const).map((v) => ({ value: v, label: t(`settings.theme.${v}` as MessageKey) }))}
                onValueChange={(v) => s.set('theme', v as typeof s.theme)}
              />
            </Row>
            <Row id="text-size" title={t('settings.textSize')} desc={t('settings.textSize.desc')}>
              <SegmentedControl
                label={t('settings.textSize')}
                value={s.textSize}
                options={(['small', 'medium', 'large'] as const).map((v) => ({ value: v, label: t(`settings.textSize.${v}` as MessageKey) }))}
                onValueChange={(v) => s.set('textSize', v as TextSize)}
              />
            </Row>
            <Row id="density" title={t('settings.density')} desc={t('settings.density.desc')}>
              <SegmentedControl
                label={t('settings.density')}
                value={s.density}
                options={(['comfortable', 'compact'] as const).map((v) => ({ value: v, label: t(`settings.density.${v}` as MessageKey) }))}
                onValueChange={(v) => s.set('density', v as Density)}
              />
            </Row>
          </section>

          <section ref={(el) => { sections.current['workspace'] = el; }} aria-labelledby="sec-workspace" className="wb-settings__section">
            <h2 id="sec-workspace">{t('settings.cat.workspace')}</h2>
            <Row id="restore-layout" title={t('settings.restoreLayout')} desc={t('settings.restoreLayout.desc')} note={t('settings.restoreLayout.restart')}>
              <Toggle label={t('settings.restoreLayout')} hideLabel checked={s.restoreLayout} onCheckedChange={(v) => s.set('restoreLayout', v)} />
            </Row>
            <Row id="reset-layout" title={t('settings.resetLayout')} desc={t('settings.resetLayout.desc')}>
              <Button
                icon={<RotateCcw />}
                onClick={() => {
                  s.resetLayout();
                  toast({ kind: 'success', title: t('toast.layoutReset') });
                }}
                data-testid="reset-layout"
              >
                {t('settings.resetLayout.button')}
              </Button>
            </Row>
          </section>

          <section ref={(el) => { sections.current['simulation'] = el; }} aria-labelledby="sec-simulation" className="wb-settings__section">
            <h2 id="sec-simulation">{t('settings.cat.simulation')}</h2>
            <Row id="time-unit" title={t('settings.timeUnit')} desc={t('settings.timeUnit.desc')}>
              <Select
                label={t('settings.timeUnit')}
                value={s.timeUnit}
                options={[
                  { value: 'auto', label: t('settings.timeUnit.auto') },
                  { value: 's', label: 's' },
                  { value: 'ms', label: 'ms' },
                  { value: 'us', label: 'µs' },
                  { value: 'ns', label: 'ns' },
                ]}
                onValueChange={(v) => s.set('timeUnit', v as TimeUnitPreference)}
              />
            </Row>
            <Row id="time-decimals" title={t('settings.timeDecimals')} desc={t('settings.timeDecimals.desc')}>
              <Select
                label={t('settings.timeDecimals')}
                value={String(s.timeDecimals)}
                options={[0, 1, 2, 3, 4, 5, 6].map((n) => ({ value: String(n), label: String(n) }))}
                onValueChange={(v) => s.set('timeDecimals', Number(v))}
              />
            </Row>
            <Row id="rate-format" title={t('settings.rateFormat')} desc={t('settings.rateFormat.desc')}>
              <SegmentedControl
                label={t('settings.rateFormat')}
                value={s.rateFormat}
                options={(['factor', 'percent'] as const).map((v) => ({ value: v, label: t(`settings.rateFormat.${v}` as MessageKey) }))}
                onValueChange={(v) => s.set('rateFormat', v as RateFormat)}
              />
            </Row>
            <div className="wb-settings__preview" aria-live="polite">
              <span className="wb-settings__preview-caption">{t('settings.preview')}</span>
              <span className="wb-readout__caption">{t('sim.simulated')}</span>
              <span className="es-mono" data-testid="time-preview">{preview}</span>
              <span className="wb-readout__caption">{t('sim.host')}</span>
              <span className="es-mono">{formatRate(0.94, s.rateFormat, t.lang)}</span>
            </div>
          </section>

          <section ref={(el) => { sections.current['about'] = el; }} aria-labelledby="sec-about" className="wb-settings__section">
            <h2 id="sec-about">{t('settings.cat.about')}</h2>
            <dl className="wb-settings__about">
              <dt>{t('settings.about.workbench')}</dt>
              <dd className="es-mono">0.1.0</dd>
              <dt>{t('settings.about.engine')}</dt>
              <dd className="es-mono">{engine ?? '—'}</dd>
              <dt>{t('settings.about.protocol')}</dt>
              <dd className="es-mono">{protocol ?? '—'}</dd>
            </dl>
            <p className="wb-setting__desc">{t('settings.about.storage')}</p>
            <Row id="reset-all" title={t('settings.resetAll')} desc={t('settings.resetAll.desc')}>
              <Button icon={<RotateCcw />} onClick={() => s.resetAll()}>
                {t('settings.resetAll')}
              </Button>
            </Row>
          </section>
        </div>
      </div>
    </div>
  );
}
