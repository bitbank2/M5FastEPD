#include "M5FastEPD.h"

m5epd_err_t __epdret__;
#define CHECK(x)                \
    __epdret__ = x;             \
    if (__epdret__ != M5EPD_OK) \
    {                           \
        return __epdret__;      \
    }

M5FastEPD::M5FastEPD(int8_t spi_index)
{
    if (spi_index > 0 && spi_index < 4)
    {
        _epd_spi = new SPIClass(spi_index);
    }
    else
    {
        _epd_spi = new SPIClass(VSPI);
    }
    _pin_cs = -1;
    _pin_busy = -1;
    _pin_sck = -1;
    _pin_mosi = -1;
    _pin_rst = -1;

    _spi_freq = 20000000;

    _rotate = IT8951_ROTATE_0;
    _direction = 1;

    _update_count = false;
    _is_reverse = false;
}

M5FastEPD::~M5FastEPD()
{
    delete _epd_spi;
}

m5epd_err_t M5FastEPD::begin(int8_t sck, int8_t mosi, int8_t miso, int8_t cs, int8_t busy, int8_t rst)
{
    _epd_spi->begin(sck, miso, mosi, 4);
    _pin_cs = cs;
    _pin_busy = busy;
    _pin_sck = sck;
    _pin_mosi = mosi;
    _pin_miso = miso;
    _pin_rst = rst;
    if (_pin_rst != -1)
    {
        pinMode(_pin_rst, OUTPUT);
        ResetDriver();
    }
    digitalWrite(_pin_cs, HIGH);
    pinMode(_pin_cs, OUTPUT);
    pinMode(_pin_busy, INPUT);

    StartSPI(10000000);

    // CHECK(GetSysInfo());
    _tar_memaddr = 0x001236E0;
    _dev_memaddr_l = 0x36E0;
    _dev_memaddr_h = 0x0012;
    CHECK(WriteCommand(IT8951_TCON_SYS_RUN));
    CHECK(WriteReg(IT8951_I80CPCR, 0x0001)); //enable pack write

    //set vcom to -2.30v
    CHECK(WriteCommand(0x0039)); //tcon vcom set command
    CHECK(WriteWord(0x0001));
    CHECK(WriteWord(2300));

    EndSPI();

    delay(1000);

    log_d("Init SUCCESS.");

    return M5EPD_OK;
}

/** @brief Invert display colors
  * @param is_reverse 1, reverse color; 0, default
  */
void M5FastEPD::SetColorReverse(bool is_reverse)
{
    _is_reverse = is_reverse;
}

/** @brief Set panel rotation
  * @param rotate direction to rotate.
  * @retval m5epd_err_t
  */
m5epd_err_t M5FastEPD::SetRotation(uint16_t rotate)
{
    if(rotate < 4)
    {
        this->_rotate = rotate;
    }
    else if(rotate < 90)
    {
        this->_rotate = IT8951_ROTATE_0;
    }
    else if(rotate < 180)
    {
        this->_rotate = IT8951_ROTATE_90;
    }
    else if(rotate < 270)
    {
        this->_rotate = IT8951_ROTATE_180;
    }
    else
    {
        this->_rotate = IT8951_ROTATE_270;
    }

    if(_rotate == IT8951_ROTATE_0 || _rotate == IT8951_ROTATE_180)
    {
        _direction = 1;
    }
    else
    {
        _direction = 0;
    }
    return M5EPD_OK;
}

/** @brief Clear graphics buffer
  * @param init Screen initialization, If is 0, clear the buffer without initializing
  * @retval m5epd_err_t
  */
m5epd_err_t M5FastEPD::Clear(bool init)
{
    uint16_t usData = 0xffff; // white
    _endian_type = IT8951_LDIMG_L_ENDIAN;
    _pix_bpp = IT8951_4BPP;

    StartSPI();

    CHECK(SetTargetMemoryAddr(_tar_memaddr));
    if(_direction)
    {
        CHECK(SetArea(0, 0, M5EPD_PANEL_W, M5EPD_PANEL_H));
    }
    else
    {
        CHECK(SetArea(0, 0, M5EPD_PANEL_H, M5EPD_PANEL_W));
    }
    if(_is_reverse)
       usData = 0x0000; // black
    digitalWrite(_pin_cs, 0);
    _epd_spi->write16(0x0000); // preamble for data write (0x0000)
    for (uint32_t x = 0; x < ((M5EPD_PANEL_W * M5EPD_PANEL_H) >> 2); x++)
    {
        _epd_spi->write16(usData);
    }
    digitalWrite(_pin_cs, 1);
    
    CHECK(WriteCommand(IT8951_TCON_LD_IMG_END));

    EndSPI();

    if(init)
    {
        CHECK(UpdateFull(UPDATE_MODE_INIT));
    }

    return M5EPD_OK;
}

/** @brief Write full (960 * 540) 4-bit (16 levels grayscale) image to panel.
  * @param gram pointer to image data.
  * @retval m5epd_err_t
  */
m5epd_err_t M5FastEPD::WriteFullGram4bpp(const uint8_t *gram)
{
    _pix_bpp = IT8951_4BPP;
    if(_direction)
    {
        return WritePartGram4bpp(0, 0, M5EPD_PANEL_W, M5EPD_PANEL_H, gram);
    }
    else
    {
        return WritePartGram4bpp(0, 0, M5EPD_PANEL_H, M5EPD_PANEL_W, gram);
    }
}

/** @brief Write the image at the specified location, Partial update
  * @param x Update X coordinate, >>> Must be a multiple of 4 <<<
  * @param y Update Y coordinate
  * @param w width of gram, >>> Must be a multiple of 4 <<<
  * @param h height of gram
  * @param gram 4bpp garm data
  * @retval m5epd_err_t
  */
m5epd_err_t M5FastEPD::WritePartGram4bpp(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint8_t *gram)
{
    _endian_type = IT8951_LDIMG_B_ENDIAN;
    _pix_bpp = IT8951_4BPP;

    // rounded up to be multiple of 4
    if(_direction)
    {
        x = (x + 3) & 0xFFFC;
    }
    else
    {
        x = (x + 3) & 0xFFFC;
        y = (y + 3) & 0xFFFC;
    }
    
    if(w & 0x03)
    {
        log_e("Gram width %d not a multiple of 4.", w);
        return M5EPD_NOTMULTIPLE4;
    }

    if(_direction)
    {
        if(x > M5EPD_PANEL_W || y > M5EPD_PANEL_H)
        {
            log_d("Pos (%d, %d) out of bounds.", x, y);
            return M5EPD_OUTOFBOUNDS;
        }
    }
    else
    {
        if(x > M5EPD_PANEL_H || y > M5EPD_PANEL_W)
        {
            log_d("Pos (%d, %d) out of bounds.", x, y);
            return M5EPD_OUTOFBOUNDS;
        }
    }
    

    uint32_t pos = 0;
    // uint64_t length = (w / 2) * h;

    StartSPI();

    uint16_t word = 0;
    const uint16_t usReverse = (_is_reverse) ? 0x0000 : 0xffff;
    CHECK(SetTargetMemoryAddr(_tar_memaddr));
    CHECK(SetArea(x, y, w, h));
    digitalWrite(_pin_cs, 0);
    _epd_spi->write16(0x0000); // data write preamble
    for (uint32_t x = 0; x < ((w * h) >> 2); x++)
    {
        word = gram[pos] << 8 | gram[pos + 1];
        word ^= usReverse;
        _epd_spi->write16(word);
        pos += 2;
    }
    digitalWrite(_pin_cs, 1);
    CHECK(WriteCommand(IT8951_TCON_LD_IMG_END));

    EndSPI();

    return M5EPD_OK;
}

/** @brief Write the image at the specified location, Partial update
  * @param x Update X coordinate, >>> Must be a multiple of 8 <<<
  * @param y Update Y coordinate
  * @param w width of gram, >>> Must be a multiple of 8 <<<
  * @param h height of gram
  * @param gram 4bpp garm data
  * @retval m5epd_err_t
  */
m5epd_err_t M5FastEPD::WritePartGram2bpp(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint8_t *gram)
{
    _endian_type = IT8951_LDIMG_B_ENDIAN;
    _pix_bpp = IT8951_2BPP;

    // rounded up to be multiple of 8
    if(_direction)
    {
        x = (x + 7) & 0xFFF8;
    }
    else
    {
        x = (x + 7) & 0xFFF8;
        y = (y + 7) & 0xFFF8;
    }
    
    if(w & 0x07)
    {
        log_e("Gram width %d not a multiple of 8.", w);
        return M5EPD_NOTMULTIPLE4;
    }

    if(_direction)
    {
        if(x > M5EPD_PANEL_W || y > M5EPD_PANEL_H)
        {
            log_d("Pos (%d, %d) out of bounds.", x, y);
            return M5EPD_OUTOFBOUNDS;
        }
    }
    else
    {
        if(x > M5EPD_PANEL_H || y > M5EPD_PANEL_W)
        {
            log_d("Pos (%d, %d) out of bounds.", x, y);
            return M5EPD_OUTOFBOUNDS;
        }
    }
    

    uint32_t pos = 0;
    // uint64_t length = (w / 2) * h;

    StartSPI();

    uint16_t word = 0;
    const uint16_t usReverse = (_is_reverse) ? 0x0000 : 0xffff;
    CHECK(SetTargetMemoryAddr(_tar_memaddr));
    CHECK(SetArea(x, y, w, h));
    digitalWrite(_pin_cs, 0);
    _epd_spi->write16(0x0000); // data write preamble
    for (uint32_t x = 0; x < ((w * h) >> 3); x++)
    {
        word = gram[pos] << 8 | gram[pos + 1];
        word ^= usReverse;
        _epd_spi->write16(word);
        pos += 2;
    }
    digitalWrite(_pin_cs, 1);
    CHECK(WriteCommand(IT8951_TCON_LD_IMG_END));

    EndSPI();

    return M5EPD_OK;
}

/** @brief Fill the color at the specified location, Partial update
  * @param x Update X coordinate, >>> Must be a multiple of 4 <<<
  * @param y Update Y coordinate
  * @param w width of gram, >>> Must be a multiple of 4 <<<
  * @param h height of gram
  * @param data 4bpp color
  * @retval m5epd_err_t
  */
m5epd_err_t M5FastEPD::FillPartGram4bpp(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t data)
{
    _endian_type = IT8951_LDIMG_B_ENDIAN;
    _pix_bpp = IT8951_4BPP;

    // rounded up to be multiple of 4
    // rounded up to be multiple of 4
    if(_direction)
    {
        x = (x + 3) & 0xFFFC;
    }
    else
    {
        x = (x + 3) & 0xFFFC;
        y = (y + 3) & 0xFFFC;
    }
    
    if(w & 0x03)
    {
        log_d("Gram width %d not a multiple of 4.", w);
        return M5EPD_NOTMULTIPLE4;
    }

    if(_direction)
    {
        if(x > M5EPD_PANEL_W || y > M5EPD_PANEL_H)
        {
            log_d("Pos (%d, %d) out of bounds.", x, y);
            return M5EPD_OUTOFBOUNDS;
        }
    }
    else
    {
        if(x > M5EPD_PANEL_H || y > M5EPD_PANEL_W)
        {
            log_d("Pos (%d, %d) out of bounds.", x, y);
            return M5EPD_OUTOFBOUNDS;
        }
    }

    // uint64_t length = (w / 2) * h;

    StartSPI();

    CHECK(SetTargetMemoryAddr(_tar_memaddr));
    CHECK(SetArea(x, y, w, h));
    digitalWrite(_pin_cs, 0);
    _epd_spi->write16(0x0000); // data write preamble
    for (uint32_t x = 0; x < ((w * h) >> 2); x++)
    {
        _epd_spi->write16(data);
    }
    digitalWrite(_pin_cs, 1);
    CHECK(WriteCommand(IT8951_TCON_LD_IMG_END));

    EndSPI();

    return M5EPD_OK;
}

/** @brief Full panel update
  * @param mode update mode
  * @retval m5epd_err_t
  */
m5epd_err_t M5FastEPD::UpdateFull(m5epd_update_mode_t mode)
{
    if(_direction)
    {
        CHECK(UpdateArea(0, 0, M5EPD_PANEL_W, M5EPD_PANEL_H, mode));
    }
    else
    {
        CHECK(UpdateArea(0, 0, M5EPD_PANEL_H, M5EPD_PANEL_W, mode));
    }
    
    return M5EPD_OK;
}

/** @brief Check if the device is busy
  * @retval m5epd_err_t
  */
m5epd_err_t M5FastEPD::CheckAFSR(void)
{
    uint32_t start_time = millis();
    while (1)
    {
        uint16_t infobuf[1];
        CHECK(WriteCommand(IT8951_TCON_REG_RD));
        CHECK(WriteWord(IT8951_LUTAFSR));
        CHECK(ReadWords(infobuf, 1));
        if(infobuf[0] == 0)
        {
            break;
        }

        if (millis() - start_time > 3000)
        {
            log_e("Device response timeout.");
            return M5EPD_BUSYTIMEOUT;
        }
    }
    return M5EPD_OK;
}

/** @brief Partial panel update
  * @param x Update X coordinate, >>> Must be a multiple of 4 <<<
  * @param y Update Y coordinate
  * @param w width of gram, >>> Must be a multiple of 4 <<<
  * @param h height of gram
  * @param mode update mode
  * @retval m5epd_err_t
  */
m5epd_err_t M5FastEPD::UpdateArea(uint16_t x, uint16_t y, uint16_t w, uint16_t h, m5epd_update_mode_t mode)
{
    if(mode == UPDATE_MODE_NONE)
    {
        return M5EPD_OTHERERR;
    }

    // rounded up to be multiple of 4
    if(_direction)
    {
        x = (x + 3) & 0xFFFC;
    }
    else
    {
        x = (x + 3) & 0xFFFC;
        y = (y + 3) & 0xFFFC;
    }

    CHECK(CheckAFSR());

    if(_direction)
    {
        if(x + w > M5EPD_PANEL_W)
        {
            w = M5EPD_PANEL_W - x;
        }
        if(y + h > M5EPD_PANEL_H)
        {
            h = M5EPD_PANEL_H - y;
        }
    }
    else
    {
        if(x + w > M5EPD_PANEL_H)
        {
            w = M5EPD_PANEL_H - x;
        }
        if(y + h > M5EPD_PANEL_W)
        {
            h = M5EPD_PANEL_W - y;
        }
    }

    uint16_t args[7];
    switch(_rotate)
    {
        case IT8951_ROTATE_0:
        {
            args[0] = x;
            args[1] = y;
            args[2] = w;
            args[3] = h;
            break;
        }
        case IT8951_ROTATE_90:
        {
            args[0] = y;
            args[1] = M5EPD_PANEL_H - w -x;
            args[2] = h;
            args[3] = w;
            break;
        }
        case IT8951_ROTATE_180:
        {
            args[0] = M5EPD_PANEL_W - w - x;
            args[1] = M5EPD_PANEL_H - h - y;
            args[2] = w;
            args[3] = h;
            break;
        }
        case IT8951_ROTATE_270:
        {
            args[0] = M5EPD_PANEL_W - h - y;
            args[1] = x;
            args[2] = h;
            args[3] = w;
            break;
        }
    }
    
    args[4] = mode;
    args[5] = _dev_memaddr_l;
    args[6] = _dev_memaddr_h;

    StartSPI();
    CHECK(WriteArgs(IT8951_I80_CMD_DPY_BUF_AREA, args, 7));
    EndSPI();

    _update_count++;

    return M5EPD_OK;
}

/** @brief  Set write area
  * @param x Update X coordinate, >>> Must be a multiple of 4 <<<
  * @param y Update Y coordinate
  * @param w width of gram, >>> Must be a multiple of 4 <<<
  * @param h height of gram
  * @retval m5epd_err_t
  */
m5epd_err_t M5FastEPD::SetArea(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
    uint16_t args[5];
    args[0] = (_endian_type << 8 | _pix_bpp << 4 | _rotate);
    args[1] = x;
    args[2] = y;
    args[3] = w;
    args[4] = h;
    CHECK(WriteArgs(IT8951_TCON_LD_IMG_AREA, args, 5));

    return M5EPD_OK;
}

/** @brief  Write image data to the set address
  * @param data pointer to 4-bpp gram data
  * @retval m5epd_err_t
  */
void M5FastEPD::WriteGramData(uint16_t data)
{
    digitalWrite(_pin_cs, 0);
    _epd_spi->write32(data);
    // _epd_spi->write16(0x0000);
    // _epd_spi->write16(data);
    digitalWrite(_pin_cs, 1);
}

m5epd_err_t M5FastEPD::SetTargetMemoryAddr(uint32_t tar_addr)
{
    uint16_t h = (uint16_t)((tar_addr >> 16) & 0x0000FFFF);
    uint16_t l = (uint16_t)(tar_addr & 0x0000FFFF);

    CHECK(WriteReg(IT8951_LISAR + 2, h));
    CHECK(WriteReg(IT8951_LISAR, l));

    return M5EPD_OK;
}


m5epd_err_t M5FastEPD::WriteReg(uint16_t addr, uint16_t data)
{
    CHECK(WriteCommand(0x0011)); //tcon write reg command
    CHECK(WriteWord(addr));
    CHECK(WriteWord(data));
    return M5EPD_OK;
}

m5epd_err_t M5FastEPD::GetSysInfo(void)
{
    uint16_t infobuf[20];
    CHECK(WriteCommand(IT8951_I80_CMD_GET_DEV_INFO));
    CHECK(ReadWords(infobuf, 20));
    _dev_memaddr_l = infobuf[2];
    _dev_memaddr_h = infobuf[3];
    _tar_memaddr = (_dev_memaddr_h << 16) | _dev_memaddr_l;
    log_d("memory addr = %04X%04X", _dev_memaddr_h, _dev_memaddr_l);
    return M5EPD_OK;
}

void M5FastEPD::ResetDriver(void)
{
    digitalWrite(_pin_rst, 1);
    digitalWrite(_pin_rst, 0);
    delay(20);
    digitalWrite(_pin_rst, 1);
    delay(100);
}

void M5FastEPD::StartSPI(uint32_t freq)
{
    _epd_spi->beginTransaction(SPISettings(freq, MSBFIRST, SPI_MODE0));
}

void M5FastEPD::StartSPI(void)
{
    _epd_spi->beginTransaction(SPISettings(_spi_freq, MSBFIRST, SPI_MODE0));
}

void M5FastEPD::EndSPI(void)
{
    _epd_spi->endTransaction();
}

m5epd_err_t M5FastEPD::WaitBusy(uint32_t timeout)
{
    uint32_t start_time = millis();
    while (1)
    {
        if (digitalRead(_pin_busy) == 1)
        {
            return M5EPD_OK;
        }

        if (millis() - start_time > timeout)
        {
            log_e("Device response timeout.");
            return M5EPD_BUSYTIMEOUT;
        }
    }
}

m5epd_err_t M5FastEPD::WriteCommand(uint16_t cmd)
{
    CHECK(WaitBusy());
    digitalWrite(_pin_cs, 0);
    _epd_spi->write16(0x6000);
    CHECK(WaitBusy());
    _epd_spi->write16(cmd);
    digitalWrite(_pin_cs, 1);

    return M5EPD_OK;
}

m5epd_err_t M5FastEPD::WriteWord(uint16_t data)
{
    CHECK(WaitBusy());
    digitalWrite(_pin_cs, 0);
    _epd_spi->write16(0x0000);
    CHECK(WaitBusy());
    _epd_spi->write16(data);
    digitalWrite(_pin_cs, 1);

    return M5EPD_OK;
}

m5epd_err_t M5FastEPD::ReadWords(uint16_t *buf, uint32_t length)
{
    // uint16_t dummy;
    CHECK(WaitBusy());
    digitalWrite(_pin_cs, 0);
    _epd_spi->write16(0x1000);
    CHECK(WaitBusy());

    //dummy
    _epd_spi->transfer16(0);
    CHECK(WaitBusy());

    for (size_t i = 0; i < length; i++)
    {
        buf[i] = _epd_spi->transfer16(0);
    }

    digitalWrite(_pin_cs, 1);
    return M5EPD_OK;
}

m5epd_err_t M5FastEPD::WriteArgs(uint16_t cmd, uint16_t *args, uint16_t length)
{
    CHECK(WriteCommand(cmd));
    for (uint16_t i = 0; i < length; i++)
    {
        CHECK(WriteWord(args[i]));
    }
    return M5EPD_OK;
}

uint16_t M5FastEPD::UpdateCount(void)
{
    return _update_count;
}

void M5FastEPD::ResetUpdateCount(void)
{
    _update_count = 0;
}
