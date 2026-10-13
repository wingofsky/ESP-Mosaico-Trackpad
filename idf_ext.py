"""Refuse generic write/erase targets before idf.py selects a serial port."""


def block_unsafe_actions(ctx, global_args, tasks):
    for task in tasks:
        name = task.name
        if name == "flash" or name.endswith("-flash") or name.startswith("erase-") or name in ("merge-bin", "dfu", "uf2"):
            raise RuntimeError("Generic flash/erase/export is disabled to preserve Recovery. Use tools/flash_app.py with a verified private backup.")


def action_extensions(base_actions, project_path):
    return {"version": "1.0.0", "global_action_callbacks": [block_unsafe_actions]}
