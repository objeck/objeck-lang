rm *.obe
obc -src compiler/parser.obs,compiler/compiler.obs,compiler/scanner.obs,compiler/emitter.obs,common/instructions.obs -lib collect.obl -dest compiler.obe
obc -src vm/interpreter.obs,common/instructions.obs -lib collect.obl -dest vm.obe
