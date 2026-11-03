// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
//
// New project: a blank sheet, or a board from the catalog to start from. The
// project is made here as JSON (SPEC-005) and loaded into the session; Save
// then writes it where the user chooses.

import { Button, Dialog, Input, useToast } from '@embsim/design';
import type { CatalogItem, Project } from '@embsim/protocol';
import { CircuitBoard, FileText } from 'lucide-react';
import { useEffect, useMemo, useState } from 'react';
import { errorMessage } from '../actions';
import { tryClient } from '../connection';
import { useLayout } from '../stores/layout';
import { useProject } from '../stores/project';
import { t as tr } from '../i18n';

/** a project with `board` (a catalog id) as B1, or none */
export function newProject(name: string, board: string | null): Project {
  return {
    schema_version: 1,
    kind: 'project',
    name,
    policy: 'electrical',
    boards: board ? [{ id: 'B1', board, layout: { x: 300, y: 240 } }] : [],
    components: [],
    nets: {},
    probes: [],
  } as unknown as Project;
}

/** a project name the schema takes: letters, digits, dot, dash and underscore */
export function projectName(text: string): string {
  const s = text
    .trim()
    .replace(/\s+/g, '-')
    .replace(/[^A-Za-z0-9._-]/g, '');
  return s || 'untitled';
}

export function NewProjectDialog() {
  const open = useLayout((s) => s.newProjectOpen);
  const setOpen = useLayout((s) => s.setNewProjectOpen);
  const loadProjectJson = useProject((s) => s.loadProjectJson);
  const current = useProject((s) => s.project?.name);
  const { toast } = useToast();
  const [boards, setBoards] = useState<CatalogItem[] | null>(null);
  const [choice, setChoice] = useState<string>('');
  const [name, setName] = useState('untitled');
  const [busy, setBusy] = useState(false);

  useEffect(() => {
    if (!open || boards) return;
    const c = tryClient();
    if (!c) return;
    c.catalog
      .list({ kind: 'board' })
      .then((r) => setBoards(r.items.filter((i) => !i.broken).sort((a, b) => a.name.localeCompare(b.name))))
      .catch(() => setBoards([]));
  }, [open, boards]);

  const options = useMemo(
    () => [{ id: '', name: tr('np.blank'), description: tr('np.blankDesc') }, ...(boards ?? [])],
    [boards],
  );

  const create = async () => {
    setBusy(true);
    try {
      const project = newProject(projectName(name), choice || null);
      await loadProjectJson(project);
      setOpen(false);
      toast({ kind: 'success', title: tr('np.created', { name: project.name }), description: choice ? tr('np.startedFrom', { board: boards?.find((b) => b.id === choice)?.name ?? choice }) : tr('np.aBlank') });
    } catch (e) {
      toast({ kind: 'error', title: tr('np.failed'), description: errorMessage(e) });
    } finally {
      setBusy(false);
    }
  };

  return (
    <Dialog
      open={open}
      onOpenChange={setOpen}
      title={tr('np.title')}
      description={current ? tr('np.replaces', { name: current }) : undefined}
      footer={
        <>
          <Button variant="ghost" onClick={() => setOpen(false)}>
            {tr('common.cancel')}
          </Button>
          <Button variant="primary" disabled={busy} onClick={() => void create()}>
            {tr('np.create')}
          </Button>
        </>
      }
    >
      <div className="wb-newproject">
        <label className="wb-newproject__name">
          <span>{tr('insp.name')}</span>
          <Input value={name} onChange={(e) => setName(e.target.value)} aria-label={tr('np.name')} />
        </label>
        <div className="wb-newproject__options" role="radiogroup" aria-label={tr('np.startFrom')}>
          {options.map((o) => (
            <label key={o.id || 'blank'} className="wb-newproject__option" data-checked={choice === o.id || undefined}>
              <input type="radio" name="new-project-start" value={o.id} checked={choice === o.id} onChange={() => setChoice(o.id)} />
              <span className="wb-newproject__icon" aria-hidden="true">
                {o.id ? <CircuitBoard /> : <FileText />}
              </span>
              <span className="wb-newproject__text">
                <span className="wb-newproject__title">{o.name}</span>
                {o.description && <span className="wb-newproject__desc">{o.description}</span>}
              </span>
            </label>
          ))}
          {boards === null && <div className="es-muted">{tr('np.loadingBoards')}</div>}
        </div>
      </div>
    </Dialog>
  );
}
