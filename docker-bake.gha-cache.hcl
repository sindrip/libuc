# Layered after docker-bake.hcl in CI only. One scope per target: a scope's
# index is last-writer-wins, so sharing one drops the other target's layers.
target "cache" {
  name = t
  matrix = {
    t = ["uapi", "libuc"]
  }
  cache-from = ["type=gha,scope=${t}"]
  cache-to   = ["type=gha,scope=${t},mode=max,ignore-error=true"]
}
