.text
.global jump_to_entry
.type jump_to_entry, %function
jump_to_entry:
    // x0 = entry_point
    // x1 = new_sp
    mov sp, x1
    br x0
