//
//  mit_data_define.h
//
//  Created by gtliu on 7/19/13.
//  Copyright (c) 2013 GT. All rights reserved.
//

#ifndef MIT_DATA_DEFINE_H
#define MIT_DATA_DEFINE_H

typedef enum MITFuncRetValue {
    MIT_RETV_SUCCESS              = 0,
    MIT_RETV_FAIL                 = -1,
    MIT_RETV_PARAM_EMPTY          = -100,
    MIT_RETV_OPEN_FILE_FAIL       = -101,
    MIT_RETV_ALLOC_MEM_FAIL       = -102
} MITFuncRetValue;

#endif
