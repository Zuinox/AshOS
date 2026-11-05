section .text
global _payload_start

_payload_start:
    incbin "payload.bin"  ; This literally pastes the bytes of the payload here