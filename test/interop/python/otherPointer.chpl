use CTypes, OS.POSIX;

export proc gimmePointer(x: c_ptr(real)) {
  var y = 4.0;
  memcpy(x, c_ptrTo(y), c_sizeof(y.type));
}

export proc writeReal(x: real) {
  writeln(x);
}
