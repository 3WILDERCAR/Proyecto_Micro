    .syntax unified
    .cpu cortex-m4
    .thumb
    
    .global Cmd_RUN
    .global Cmd_CALL

    .section .text 
    .type Cmd_RUN, %function
    .type Cmd_CALL, %function

Cmd_RUN:
		push {lr}                   // Convenio
		
		// r0 contiene la dirección de interés, la enviamos desde C, ya con el LSB en 1
        
		str r0, [sp]                // Cambiamos el LR original en el stack con nuestra dirección destino
        
		//mov lr, #0 para no regresar y aislar RUN del resto del programa
        
		// Convenio, aunque en este caso, la dirección a la que nos estamos dirigiendo no es la de retorno
        // Este pop es para saltar a la dirección que pusimos en el stack usando r0, que es la dirección que escribimos en la terminal
        pop {pc}

Cmd_CALL:
        push {lr}                   // Convenio

        blx r0                      // Saltamos a la dirección en r0 y actualizamos automáticamente el LR
        
        pop {pc}                    // Este pop nos permite ahora sí, volver al monitor