/*
 * This file is part of the micropython-ulab project, 
 *
 * https://github.com/v923z/micropython-ulab
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2019 Zoltán Vörös
*/
    
#include <stdlib.h>
#include <string.h>
#include "py/obj.h"
#include "py/runtime.h"
#include "py/misc.h"
#include "linalg.h"

mp_obj_t linalg_transpose(mp_obj_t self_in) {
    ndarray_obj_t *self = MP_OBJ_TO_PTR(self_in);
    // the size of a single item in the array
    uint8_t _sizeof = mp_binary_get_size('@', self->array->typecode, NULL);
    
    // NOTE: In principle, we could simply specify the stride direction, and then we wouldn't 
    // even have to shuffle the elements. The downside of that approach is that we would have 
    // to implement two versions of the matrix multiplication and inversion functions
    
    // NOTE: 
    // if the matrices are square, we can simply swap items, but 
    // generic matrices can't be transposed in place, so we have to 
    // declare a temporary variable
    
    // NOTE: 
    //  In the old matrix, the coordinate (m, n) is m*self->n + n
    //  We have to assign this to the coordinate (n, m) in the new 
    //  matrix, i.e., to n*self->m + m
    
    // one-dimensional arrays can be transposed by simply swapping the dimensions
    if((self->m != 1) && (self->n != 1)) {
        uint8_t *c = (uint8_t *)self->array->items;
        // self->bytes is the size of the bytearray, irrespective of the typecode
        uint8_t *tmp = m_new(uint8_t, self->bytes);
        for(size_t m=0; m < self->m; m++) {
            for(size_t n=0; n < self->n; n++) {
                memcpy(tmp+_sizeof*(n*self->m + m), c+_sizeof*(m*self->n + n), _sizeof);
            }
        }
        memcpy(self->array->items, tmp, self->bytes);
        m_del(uint8_t, tmp, self->bytes);
    } 
    SWAP(size_t, self->m, self->n);
    return mp_const_none;
}

mp_obj_t linalg_reshape(mp_obj_t self_in, mp_obj_t shape) {
    ndarray_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if(!MP_OBJ_IS_TYPE(shape, &mp_type_tuple) || (MP_OBJ_SMALL_INT_VALUE(mp_obj_len_maybe(shape)) != 2)) {
        mp_raise_ValueError("shape must be a 2-tuple");
    }

    mp_obj_iter_buf_t iter_buf;
    mp_obj_t item, iterable = mp_getiter(shape, &iter_buf);
    uint16_t m, n;
    item = mp_iternext(iterable);
    m = mp_obj_get_int(item);
    item = mp_iternext(iterable);
    n = mp_obj_get_int(item);
    if(m*n != self->m*self->n) {
        // TODO: the proper error message would be "cannot reshape array of size %d into shape (%d, %d)"
        mp_raise_ValueError("cannot reshape array (incompatible input/output shape)");
    }
    self->m = m;
    self->n = n;
    return MP_OBJ_FROM_PTR(self);
}

bool linalg_invert_matrix(float *data, size_t N) {
    // returns true, of the inversion was successful, 
    // false, if the matrix is singular
    
    // initially, this is the unit matrix: the contents of this matrix is what 
    // will be returned after all the transformations
    float *unit = m_new(float, N*N);

    float elem = 1.0;
    // initialise the unit matrix
    memset(unit, 0, sizeof(float)*N*N);
    for(size_t m=0; m < N; m++) {
        memcpy(&unit[m*(N+1)], &elem, sizeof(float));
    }
    for(size_t m=0; m < N; m++){
        // this could be faster with ((c < epsilon) && (c > -epsilon))
        if(abs(data[m*(N+1)]) < epsilon) {
            m_del(float, unit, N*N);
            return false;
        }
        for(size_t n=0; n < N; n++){
            if(m != n){
                elem = data[N*n+m] / data[m*(N+1)];
                for(size_t k=0; k < N; k++){
                    data[N*n+k] -= elem * data[N*m+k];
                    unit[N*n+k] -= elem * unit[N*m+k];
                }
            }
        }
    }
    for(size_t m=0; m < N; m++){ 
        elem = data[m*(N+1)];
        for(size_t n=0; n < N; n++){
            data[N*m+n] /= elem;
            unit[N*m+n] /= elem;
        }
    }
    memcpy(data, unit, sizeof(float)*N*N);
    m_del(float, unit, N*N);
    return true;
}

mp_obj_t linalg_inv(mp_obj_t o_in) {
    // since inv is not a class method, we have to inspect the input argument first
    if(!MP_OBJ_IS_TYPE(o_in, &ulab_ndarray_type)) {
        mp_raise_TypeError("only ndarrays can be inverted");
    }
    ndarray_obj_t *o = MP_OBJ_TO_PTR(o_in);
    if(!MP_OBJ_IS_TYPE(o_in, &ulab_ndarray_type)) {
        mp_raise_TypeError("only ndarray objects can be inverted");
    }
    if(o->m != o->n) {
        mp_raise_ValueError("only square matrices can be inverted");
    }
    ndarray_obj_t *inverted = create_new_ndarray(o->m, o->n, NDARRAY_FLOAT);
    float *data = (float *)inverted->array->items;
    mp_obj_t elem;
    for(size_t m=0; m < o->m; m++) { // rows first
        for(size_t n=0; n < o->n; n++) { // columns next
            // this could, perhaps, be done in single line... 
            // On the other hand, we probably spend little time here
            elem = mp_binary_get_val_array(o->array->typecode, o->array->items, m*o->n+n);
            data[m*o->n+n] = (float)mp_obj_get_float(elem);
        }
    }
    
    if(!linalg_invert_matrix(data, o->m)) {
        // TODO: I am not sure this is needed here. Otherwise, 
        // how should we free up the unused RAM of inverted?
        m_del(float, inverted->array->items, o->n*o->n);
        mp_raise_ValueError("input matrix is singular");
    }
    return MP_OBJ_FROM_PTR(inverted);
}

mp_obj_t linalg_dot(mp_obj_t _m1, mp_obj_t _m2) {
    // TODO: should the results be upcast?
    ndarray_obj_t *m1 = MP_OBJ_TO_PTR(_m1);
    ndarray_obj_t *m2 = MP_OBJ_TO_PTR(_m2);    
    if(m1->n != m2->m) {
        mp_raise_ValueError("matrix dimensions do not match");
    }
    ndarray_obj_t *out = create_new_ndarray(m1->m, m2->n, NDARRAY_FLOAT);
    float *outdata = (float *)out->array->items;
    float sum, v1, v2;
    for(size_t i=0; i < m1->n; i++) {
        for(size_t j=0; j < m2->m; j++) {
            sum = 0.0;
            for(size_t k=0; k < m1->m; k++) {
                // (j, k) * (k, j)
                v1 = ndarray_get_float_value(m1->array->items, m1->array->typecode, i*m1->n+k);
                v2 = ndarray_get_float_value(m2->array->items, m2->array->typecode, k*m2->n+j);
                sum += v1 * v2;
            }
            outdata[i*m1->m+j] = sum;
        }
    }
    return MP_OBJ_FROM_PTR(out);
}

mp_obj_t linalg_zeros_ones(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args, uint8_t kind) {
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_, MP_ARG_REQUIRED | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} } ,
        { MP_QSTR_dtype, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = NDARRAY_FLOAT} },
    };

    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);
    
    uint8_t dtype = args[1].u_int;
    if(!mp_obj_is_int(args[0].u_obj) && !mp_obj_is_type(args[0].u_obj, &mp_type_tuple)) {
        mp_raise_TypeError("input argument must be an integer or a 2-tuple");
    }
    ndarray_obj_t *ndarray = NULL;
    if(mp_obj_is_int(args[0].u_obj)) {
        size_t n = mp_obj_get_int(args[0].u_obj);
        ndarray = create_new_ndarray(1, n, dtype);
    } else if(mp_obj_is_type(args[0].u_obj, &mp_type_tuple)) {
        mp_obj_tuple_t *tuple = MP_OBJ_TO_PTR(args[0].u_obj);
        if(tuple->len != 2) {
            mp_raise_TypeError("input argument must be an integer or a 2-tuple");            
        }
        ndarray = create_new_ndarray(mp_obj_get_int(tuple->items[0]), 
                                                  mp_obj_get_int(tuple->items[1]), dtype);
    }
    if(kind == 1) {
        mp_obj_t one = mp_obj_new_int(1);
        for(size_t i=0; i < ndarray->array->len; i++) {
            mp_binary_set_val_array(dtype, ndarray->array->items, i, one);
        }
    }
    return MP_OBJ_FROM_PTR(ndarray);
}

mp_obj_t linalg_zeros(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    return linalg_zeros_ones(n_args, pos_args, kw_args, 0);
}

mp_obj_t linalg_ones(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    return linalg_zeros_ones(n_args, pos_args, kw_args, 1);
}

mp_obj_t linalg_eye(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_, MP_ARG_REQUIRED | MP_ARG_INT, {.u_int = 0} },
        { MP_QSTR_M, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_rom_obj = MP_ROM_PTR(&mp_const_none_obj) } },
        { MP_QSTR_k, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 0} },        
        { MP_QSTR_dtype, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = NDARRAY_FLOAT} },
    };

    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    size_t n = args[0].u_int, m;
    int16_t k = args[2].u_int;
    uint8_t dtype = args[3].u_int;
    if(args[1].u_rom_obj == mp_const_none) {
        m = n;
    } else {
        m = mp_obj_get_int(args[1].u_rom_obj);
    }
    
    ndarray_obj_t *ndarray = create_new_ndarray(m, n, dtype);
    mp_obj_t one = mp_obj_new_int(1);
    size_t i = 0;
    if((k >= 0) && (k < n)) {
        while(k < n) {
            mp_binary_set_val_array(dtype, ndarray->array->items, i*n+k, one);
            k++;
            i++;
        }
    } else if((k < 0) && (-k < m)) {
        k = -k;
        i = 0;
        while(k < m) {
            mp_binary_set_val_array(dtype, ndarray->array->items, k*n+i, one);
            k++;
            i++;
        }
    }
    return MP_OBJ_FROM_PTR(ndarray);
}

mp_obj_t linalg_det(mp_obj_t oin) {
    if(!mp_obj_is_type(oin, &ulab_ndarray_type)) {
        mp_raise_TypeError("function defined for ndarrays only");
    }
    ndarray_obj_t *in = MP_OBJ_TO_PTR(oin);
    if(in->m != in->n) {
        mp_raise_ValueError("input must be square matrix");
    }
    
    float *tmp = m_new(float, in->n*in->n);
    for(size_t i=0; i < in->array->len; i++){
        tmp[i] = ndarray_get_float_value(in->array->items, in->array->typecode, i);
    }
    float c;
    for(size_t m=0; m < in->m-1; m++){
        if(abs(tmp[m*(in->n+1)]) < epsilon) {
            m_del(float, tmp, in->n*in->n);
            mp_raise_ValueError("singular matrix");
        }
        for(size_t n=0; n < in->n; n++){
            if(m != n) {
                c = tmp[in->n*n+m] / tmp[m*(in->n+1)];
                for(size_t k=0; k < in->n; k++){
                    tmp[in->n*n+k] -= c * tmp[in->n*m+k];
                }
            }
        }
    }
    float det = 1.0;
                            
    for(size_t m=0; m < in->m; m++){ 
        det *= tmp[m*(in->n+1)];
    }
    m_del(float, tmp, in->n*in->n);
    return mp_obj_new_float(det);
}
