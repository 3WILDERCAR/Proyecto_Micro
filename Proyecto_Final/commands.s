        .syntax unified
        .cpu cortex-m4
        .thumb
        
        .global Cmd_RD
        .global Cmd_RM
        .extern cpu_regs     // Referencia al arreglo global en C
        .extern Print_RD     // Referencia a la función de impresión en C

        .section .text 
        .type Cmd_RD, %function
        .type Cmd_RM, %function

Cmd_RD:
        push {lr}                   // Hacemos push del LR para preservar el valor de retorno
        push {r0}                   // Guardamos en el stack el valor original de r0 para poder mostrarlo al final

        ldr r0, =cpu_regs           // R0 será quien apunte al arreglo cpu_regs en C
        
        // Etapa READ (STORE): Leemos los registros y los escribimos en el arreglo que definimos en C

        str r1, [r0, #4]
        str r2, [r0, #8]
        str r3, [r0, #12]
        str r4, [r0, #16]
        str r5, [r0, #20]
        str r6, [r0, #24]
        str r7, [r0, #28]
        str r8, [r0, #32]
        str r9, [r0, #36]
        str r10, [r0, #40]
        str r11, [r0, #44]
        str r12, [r0, #48]

        // Recuperamos el R0 original del stack y lo guardamos en la posición 0
        pop {r1}
        str r1, [r0, #0]            // Ahora la posición inicial cpu_regs[0] es el R0 original

        // Extraemos los registros del sistema (Un poco más complejos)

        mov r1, sp                  // Leemos el Stack Pointer actual (registro 13)
        add r1, r1, #4              // Le sumamos 4 para compensar el "push {lr}" del principio y así poder mostrar tener el SP real
        str r1, [r0, #52]           // Guardamos SP en el arreglo
        
        ldr r1, [sp]                // Leemos el LR original directamente desde el tope del stack debido al push inicial
        str r1, [r0, #56]           // Guardamos LR (registro 14)
        
        mov r1, pc                  // Leemos el Program Counter
        str r1, [r0, #60]           // Guardamos PC (registro 15)
        
        mrs r1, xpsr                // Leemos el Program Status Register (instrucción "MRS" - Move to Register from Special register) y Usamos eXtended PSR
        str r1, [r0, #64]           // Usamos xpsr en lugar de psr para darle a entender al compi que buscamos guardar PSR completo APSR, IPSR y EPSR (registro 16)

        bl Print_RD                 // Saltamos de vuelta a C para imprimir la tabla

        // No hay etapa WRITE porque no es necesario en RD

        pop {pc}                    // pop {pc} para retornar


Cmd_RM:
        push {lr}
        
        // r0 contiene el indice del registro (0 al 16)
        // r1 contiene el dato a escribir

        cmp r0, #0                  // cmp se utiliza para comparar 2 operandos, entiéndase, restarlos para verificar si el resultado es 0 y actualiza las flags
        beq Set_r0
        cmp r0, #1
        beq Set_r1
        cmp r0, #2
        beq Set_r2
        cmp r0, #3
        beq Set_r3
        cmp r0, #4
        beq Set_r4
        cmp r0, #5
        beq Set_r5
        cmp r0, #6
        beq Set_r6
        cmp r0, #7
        beq Set_r7
        cmp r0, #8
        beq Set_r8
        cmp r0, #9
        beq Set_r9
        cmp r0, #10
        beq Set_r10
        cmp r0, #11
        beq Set_r11
        cmp r0, #12
        beq Set_r12
        cmp r0, #13
        beq Set_sp
        cmp r0, #14
        beq Set_lr
        cmp r0, #15
        beq Set_pc
        cmp r0, #16
        beq Set_psr
        
        b RM_end                 // Si hay un error, salimos

Set_r0:
        mov r0, r1
        b RM_end

Set_r1:
        mov r1, r1              // r1 ya tenía el valor, pero para mantener la "simetría" del código!
        b RM_end

Set_r2:
        mov r2, r1
        b RM_end

Set_r3:
        mov r3, r1
        b RM_end

Set_r4:
        mov r4, r1
        b RM_end

Set_r5:
        mov r5, r1
        b RM_end

Set_r6:
        mov r6, r1
        b RM_end

Set_r7:
        mov r7, r1
        b RM_end

Set_r8:
        mov r8, r1
        b RM_end

Set_r9:
        mov r9, r1
        b RM_end

Set_r10:
        mov r10, r1
        b RM_end

Set_r11:
        mov r11, r1
        b RM_end

Set_r12:
        mov r12, r1
        b RM_end

Set_sp:
        mov sp, r1
        b RM_end

Set_lr:
        mov lr, r1
        b RM_end

Set_pc:
        mov pc, r1
        b RM_end

Set_psr:
        msr xpsr, r1 // MSR para transferir a registros especiales
        b RM_end

RM_end:
    pop {pc}