# AGENTS.md

Reglas para agentes que trabajen en este repositorio.

## Este repo (origin) si: commits, push y tags

`origin` es del usuario (`github.com:icarito/godot-box3d-3`). Se puede y se
espera que el agente:

- Haga commits locales.
- Haga `git push` a `origin`, incluida la rama de trabajo.
- Cree y pushee tags en `origin` para disparar el release del CI; el agente no
  sube assets a mano. Hay un release por perfil (ver `docs/build-profiles.md`):
  - `vX.Y.Z` (o con guion, p. ej. `v0.4.6-nightly10`): motor completo para
    Odisea + TheGates.
  - `vX.Y.Z-xmpp`: templates para xat (módulo xmpp).
  - `vX.Y.Z-lite`: binario FRT para gdtk (imagen Arch para wlroots).

## Builds

Los builds se organizan por perfiles (`scripts/profiles.sh`), uno por
consumidor, para no servir el mismo binario a todos. Antes de tocar
`scripts/build.sh` o los workflows, leer `docs/build-profiles.md`.

## Repos de terceros / upstream: solo lectura

NUNCA, sin excepciones, hacer operaciones de **escritura** en repos que no son
del usuario:

- Abrir, crear, editar, comentar, aprobar, mergear o cerrar **PRs** o **issues**.
- Hacer `git push` a un remote upstream o de terceros (`erincatto/box3d`,
  `godotengine/godot`, `efornara/frt`, los repos de PortMaster, etc.).
- Publicar releases, tags, assets o gists en un repo ajeno.
- Usar `gh`, tokens, claves SSH o cualquier credencial del usuario para esas
  operaciones.
- Abrir un PR upstream "en su nombre" (por ejemplo a `erincatto/box3d`) por mas
  que el cambio parezca correcto, bien testeado o que el usuario haya pedido
  explorar/vender el cambio. La autoria y la cuenta son del usuario.

Se permite **solo lectura** en repos de terceros cuando forme parte de la tarea:
`gh pr list`, `gh issue view`, `gh search`, `git fetch`, `git log`, `git show`.

Si un cambio amerita un PR upstream, el entregable es el commit y el push a
`origin` en una rama propia mas una descripcion del mismo; el PR upstream lo
abre el usuario, nunca el agente.
