# AGENTS.md

Reglas para agentes que trabajen en este repositorio.

## Prohibido actuar en remotos / GitHub en nombre del usuario

NUNCA, sin excepciones, hacer ninguna de estas cosas:

- Abrir, crear, editar, comentar, aprobar, mergear o cerrar **PRs** o **issues**.
- Hacer `git push` a cualquier remote, incluido el fork propio (`origin` de este
  repo) y repos ajenos.
- Publicar releases, tags, assets o gists.
- Usar `gh`, tokens, claves SSH o cualquier credencial del usuario para
  operaciones de **escritura**.
- Abrir un PR upstream "en su nombre" (por ejemplo a `erincatto/box3d`) por mas
  que el cambio parezca correcto, bien testeado o que el usuario haya pedido
  explorar/vender el cambio. La autoria y la cuenta son del usuario.

Se permite **solo lectura** cuando forme parte de la tarea: `gh pr list`,
`gh issue view`, `gh search`, `git fetch`, `git log`, `git show`.

Los commits **locales** estan permitidos cuando el usuario los pide. Cualquier
accion que salga hacia un remoto la ejecuta el usuario, nunca el agente.

Si un cambio amerita un PR upstream, el entregable es el commit local en una
rama y una descripcion del mismo; el PR lo abre el usuario.
